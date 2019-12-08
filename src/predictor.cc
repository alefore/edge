#include "src/predictor.h"

#include <algorithm>
#include <cstring>
#include <iostream>
#include <list>
#include <memory>
#include <string>

extern "C" {
#include <dirent.h>
#include <libgen.h>
#include <sys/types.h>
}

#include "src/buffer.h"
#include "src/buffer_variables.h"
#include "src/char_buffer.h"
#include "src/dirname.h"
#include "src/editor.h"
#include "src/file_link_mode.h"
#include "src/lowercase.h"
#include "src/predictor.h"
#include "src/wstring.h"

namespace afc::editor {
namespace {

using afc::editor::EditorState;
using afc::editor::Line;
using afc::editor::OpenBuffer;
using afc::editor::Predictor;
using std::cout;
using std::function;
using std::min;
using std::shared_ptr;
using std::sort;
using std::wstring;

void HandleEndOfFile(OpenBuffer* buffer,
                     std::function<void(PredictResults)> consumer) {
  CHECK(buffer != nullptr);

  LOG(INFO) << "Predictions buffer received end of file. Predictions: "
            << buffer->contents()->size();
  buffer->SortContents(
      LineNumber(0), buffer->EndLine(),
      [](const shared_ptr<const Line>& a, const shared_ptr<const Line>& b) {
        return *LowerCase(a->contents()) < *LowerCase(b->contents());
      });

  LOG(INFO) << "Removing duplicates.";
  for (auto line = LineNumber(0);
       line.ToDelta() < buffer->contents()->size();) {
    if (line == LineNumber(0) || buffer->LineAt(line.previous())->ToString() !=
                                     buffer->LineAt(line)->ToString()) {
      line++;
    } else {
      buffer->EraseLines(line, line.next());
    }
  }

  wstring common_prefix = buffer->contents()->front()->contents()->ToString();
  PredictResults predict_results;
  if (buffer->contents()->EveryLine([&common_prefix](LineNumber,
                                                     const Line& line) {
        if (line.empty()) {
          return true;
        }
        VLOG(5) << "Considering prediction: " << line.ToString()
                << " (end column: " << line.EndColumn() << ")";
        size_t current_size =
            min(common_prefix.size(), line.EndColumn().column);
        wstring current =
            line.Substring(ColumnNumber(0), ColumnNumberDelta(current_size))
                ->ToString();

        auto prefix_end =
            mismatch(common_prefix.begin(), common_prefix.end(),
                     current.begin(), [](wchar_t common_c, wchar_t current_c) {
                       return towlower(common_c) == towlower(current_c);
                     });
        if (prefix_end.first != common_prefix.end()) {
          if (prefix_end.first == common_prefix.begin()) {
            LOG(INFO) << "Aborting completion.";
            return false;
          }
          common_prefix = wstring(common_prefix.begin(), prefix_end.first);
        }
        return true;
      })) {
    predict_results.common_prefix = common_prefix;
  }

  consumer(std::move(predict_results));
}

std::wstring GetPrompt(const OpenBuffer& buffer) {
  return buffer.LineAt(LineNumber(0))->ToString();
}

// Wrap `consumer` with a consumer that verifies that `prompt_buffer` hasn't
// expired and that its text hasn't changed.
std::function<void(PredictResults)> GuardConsumer(
    std::shared_ptr<OpenBuffer> prompt_buffer,
    std::function<void(PredictResults)> consumer) {
  std::wstring initial_text = GetPrompt(*prompt_buffer);
  std::weak_ptr<OpenBuffer> weak_prompt_buffer = prompt_buffer;
  return [consumer, weak_prompt_buffer,
          initial_text](PredictResults predict_results) {
    auto prompt_buffer = weak_prompt_buffer.lock();
    if (prompt_buffer == nullptr) {
      LOG(INFO) << "Prompt buffer has expired.";
      return;
    }
    auto current_text = GetPrompt(*prompt_buffer);
    if (current_text != initial_text) {
      LOG(INFO) << "Text has changed from \"" << initial_text << "\" to \""
                << current_text << "\"";
      return;
    }
    LOG(INFO) << "Running on prediction: "
              << predict_results.common_prefix.value_or(L"<missing value>");
    consumer(std::move(predict_results));
  };
}
}  // namespace

std::ostream& operator<<(std::ostream& os, const PredictResults& lc) {
  os << "[PredictResults";
  if (lc.common_prefix.has_value()) {
    os << lc.common_prefix.value();
  }
  os << "]";
  return os;
}

void Predict(PredictOptions options) {
  auto shared_status = std::make_shared<Status>(
      options.editor_state->GetConsole(), options.editor_state->audio_player());
  shared_status->CopyFrom(*options.status);
  std::shared_ptr<OpenBuffer>& predictions_buffer =
      (*options.editor_state->buffers())[PredictionsBufferName()];
  OpenBuffer::Options buffer_options;
  buffer_options.editor = options.editor_state;
  buffer_options.name = PredictionsBufferName();
  auto weak_predictions_buffer = std::make_shared<std::weak_ptr<OpenBuffer>>();

  options.callback = GuardConsumer(shared_status->prompt_buffer(),
                                   std::move(options.callback));
  buffer_options.generate_contents = [options, weak_predictions_buffer,
                                      shared_status](OpenBuffer* buffer) {
    auto shared_predictions_buffer = weak_predictions_buffer->lock();
    CHECK_EQ(shared_predictions_buffer.get(), buffer);
    if (options.editor_state->status()->prompt_buffer() == nullptr) {
      buffer->status()->CopyFrom(*shared_status);
    }
    auto prompt = shared_status->prompt_buffer();
    CHECK(prompt != nullptr);
    options.predictor(
        options.editor_state, prompt->LineAt(LineNumber(0))->ToString(), buffer,
        [shared_status, buffer, options, shared_predictions_buffer] {
          buffer->set_current_cursor(LineColumn());
          HandleEndOfFile(buffer, options.callback);
        });
  };
  predictions_buffer = std::make_shared<OpenBuffer>(std::move(buffer_options));
  *weak_predictions_buffer = predictions_buffer;
  predictions_buffer->Set(buffer_variables::show_in_buffers_list, false);
  predictions_buffer->Set(buffer_variables::allow_dirty_delete, true);
  predictions_buffer->Reload();
  if (options.editor_state->status()->prompt_buffer() == nullptr) {
    predictions_buffer->status()->CopyFrom(*shared_status);
  }
}

void FilePredictor(EditorState* editor_state, const wstring& input_path,
                   OpenBuffer* buffer, std::function<void()> callback) {
  LOG(INFO) << "Generating predictions for: " << input_path;
  struct AsyncInput {
    OpenBuffer* buffer;
    wstring path;
    vector<wstring> search_paths;
    ResolvePathOptions resolve_path_options;
    std::function<void()> callback;
  };

  AsyncProcessor<AsyncInput, int>::Options options;
  options.name = L"FilePredictor";
  options.factory = [](AsyncInput input) {
    std::wstring path = input.path;
    if (!path.empty() && *path.begin() == L'/') {
      input.search_paths = {L"/"};
    } else {
      for (auto& search_path : input.search_paths) {
        search_path = Realpath(search_path.empty() ? L"." : search_path);
      }

      std::set<std::wstring> unique_paths_set;
      std::vector<std::wstring> unique_paths;  // Preserve the order.
      for (const auto& search_path : input.search_paths) {
        if (unique_paths_set.insert(search_path).second) {
          unique_paths.push_back(search_path);
        }
      }

      input.search_paths = std::move(unique_paths);
    }

    for (const auto& search_path : input.search_paths) {
      VLOG(4) << "Considering search path: " << search_path;
      wstring path_with_prefix = search_path;
      if (search_path.empty()) {
        path_with_prefix = path.empty() ? L"." : path;
      } else if (!path.empty()) {
        path_with_prefix += L"/" + path;
      }

      VLOG(6) << "Starting search at: " << search_path;
      auto parent_dir = OpenDir(search_path);
      if (parent_dir == nullptr) {
        VLOG(5) << "Unable to open search_path: " << search_path;
        continue;
      }
      std::list<std::wstring> components;
      DirectorySplit(input.path, &components);
      std::wstring prefix;

      while (components.size() > 1) {
        auto subdir_path = PathJoin(prefix, components.front());
        auto subdir = OpenDir(PathJoin(search_path, subdir_path));
        if (subdir != nullptr) {
          parent_dir = std::move(subdir);
          prefix = std::move(subdir_path);
          components.pop_front();
        } else {
          break;
        }
      }
      VLOG(5) << "After descent: " << prefix;

      if (prefix.empty()) {
        path_with_prefix = search_path;
      } else {
        auto resolve_path_options = input.resolve_path_options;
        resolve_path_options.path = prefix;
        resolve_path_options.search_paths = {search_path};
        if (auto results = ResolvePath(resolve_path_options);
            results.has_value()) {
          path_with_prefix = results->path;
        } else {
          LOG(INFO) << "Unable to resolve, giving up current search path.";
          continue;
        }
      }

      // The length of the longest prefix of `basename_prefix` that matches an
      // entry in `parent_dir`.
      size_t longest_prefix_match = 0;
      auto basename_prefix = components.front();
      struct dirent* entry;
      while ((entry = readdir(parent_dir.get())) != nullptr) {
        string entry_path = entry->d_name;

        auto mismatch_results =
            std::mismatch(basename_prefix.begin(), basename_prefix.end(),
                          entry_path.begin(), entry_path.end());
        if (mismatch_results.first != basename_prefix.end()) {
          VLOG(20) << "The entry " << entry_path
                   << " doesn't contain the whole prefix.";
          longest_prefix_match = std::max<int>(
              longest_prefix_match,
              std::distance(basename_prefix.begin(), mismatch_results.first));
          continue;
        }
        wstring prediction =
            PathJoin(path_with_prefix, FromByteString(entry->d_name)) +
            (entry->d_type == DT_DIR ? L"/" : L"");
        if (!search_path.empty() && search_path != L"/" &&
            std::equal(search_path.begin(), search_path.end(),
                       prediction.begin())) {
          VLOG(6) << "Removing prefix from prediction: " << prediction;
          size_t start = prediction.find_first_not_of('/', search_path.size());
          if (start != prediction.npos) {
            prediction = prediction.substr(start);
          }
        } else {
          VLOG(6) << "Not stripping prefix " << search_path
                  << " from prediction " << prediction;
        }
        VLOG(5) << "Prediction: " << prediction;
        input.buffer->SchedulePendingWork(
            [buffer = input.buffer,
             line = std::shared_ptr<LazyString>(NewLazyString(prediction))] {
              buffer->AppendToLastLine(line);
              buffer->AppendRawLine(std::make_shared<Line>(Line::Options()));
            });
      }
    }
    input.buffer->SchedulePendingWork(
        [buffer = input.buffer, callback = input.callback] {
          LOG(INFO) << "Signaling end of file.";
          buffer->EndOfFile();
          buffer->AddEndOfFileObserver(callback);
        });
    return 0;
  };
  static AsyncProcessor<AsyncInput, int> async_processor(std::move(options));

  AsyncInput input{buffer,
                   editor_state->expand_path(input_path),
                   {},
                   ResolvePathOptions::New(editor_state),
                   std::move(callback)};
  GetSearchPaths(editor_state, &input.search_paths);
  async_processor.Push(std::move(input));
}

void EmptyPredictor(EditorState*, const wstring&, OpenBuffer* buffer,
                    std::function<void()> callback) {
  buffer->EndOfFile();
  buffer->AddEndOfFileObserver(callback);
}

namespace {
void RegisterVariations(const wstring& prediction, wchar_t separator,
                        vector<wstring>* output) {
  size_t start = 0;
  DVLOG(5) << "Generating predictions for: " << prediction;
  while (true) {
    start = prediction.find_first_not_of(separator, start);
    if (start == wstring::npos) {
      return;
    }
    output->push_back(prediction.substr(start));
    DVLOG(6) << "Prediction: " << output->back();
    start = prediction.find_first_of(separator, start);
    if (start == wstring::npos) {
      return;
    }
  }
}

}  // namespace

const wstring& PredictionsBufferName() {
  static wstring output = L"- predictions";
  return output;
}

Predictor PrecomputedPredictor(const vector<wstring>& predictions,
                               wchar_t separator) {
  const auto contents =
      std::make_shared<multimap<wstring, shared_ptr<LazyString>>>();
  for (const auto& prediction : predictions) {
    vector<wstring> variations;
    RegisterVariations(prediction, separator, &variations);
    for (auto& variation : variations) {
      contents->insert(make_pair(variation, NewLazyString(prediction)));
    }
  }
  return [contents](EditorState*, const wstring& input, OpenBuffer* buffer,
                    std::function<void()> callback) {
    for (auto it = contents->lower_bound(input); it != contents->end(); ++it) {
      auto result = mismatch(input.begin(), input.end(), (*it).first.begin());
      if (result.first == input.end()) {
        buffer->AppendToLastLine(it->second);
        buffer->AppendRawLine(std::make_shared<Line>(Line::Options()));
      } else {
        break;
      }
    }
    buffer->EndOfFile();
    buffer->AddEndOfFileObserver(callback);
  };
}

}  // namespace afc::editor
