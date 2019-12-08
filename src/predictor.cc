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
                     std::function<void(wstring)> consumer) {
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
  bool results = buffer->contents()->EveryLine(
      [&common_prefix](LineNumber, const Line& line) {
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
      });
  if (results) {
    consumer(common_prefix);
  } else {
    auto editor_state = buffer->editor();
    auto buffers = editor_state->buffers();
    auto name = buffer->Read(buffer_variables::name);
    if (auto it = buffers->find(name); it != editor_state->buffers()->end()) {
      CHECK_EQ(buffer, it->second.get());
      editor_state->set_current_buffer(it->second);
      buffer->set_current_position_line(LineNumber(0));
    } else {
      buffer->status()->SetWarningText(
          L"Error: EndOfFile: predictions buffer not found: name");
    }
  }
}

std::wstring GetPrompt(const OpenBuffer& buffer) {
  return buffer.LineAt(LineNumber(0))->ToString();
}

// Wrap `consumer` with a consumer that verifies that `prompt_buffer` hasn't
// expired and that its text hasn't changed.
std::function<void(const wstring&)> GuardConsumer(
    std::shared_ptr<OpenBuffer> prompt_buffer,
    std::function<void(const wstring&)> consumer) {
  std::wstring initial_text = GetPrompt(*prompt_buffer);
  std::weak_ptr<OpenBuffer> weak_prompt_buffer = prompt_buffer;
  return
      [consumer, weak_prompt_buffer, initial_text](const wstring& prediction) {
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
        LOG(INFO) << "Running on prediction: " << prediction;
        consumer(prediction);
      };
}
}  // namespace

void Predict(EditorState* editor_state, Predictor predictor, Status* status,
             function<void(const wstring&)> consumer) {
  auto shared_status = std::make_shared<Status>(editor_state->GetConsole(),
                                                editor_state->audio_player());
  shared_status->CopyFrom(*status);
  std::shared_ptr<OpenBuffer>& predictions_buffer =
      (*editor_state->buffers())[PredictionsBufferName()];
  OpenBuffer::Options options;
  options.editor = editor_state;
  options.name = PredictionsBufferName();
  auto weak_predictions_buffer = std::make_shared<std::weak_ptr<OpenBuffer>>();

  consumer = GuardConsumer(shared_status->prompt_buffer(), std::move(consumer));
  options.generate_contents = [editor_state, predictor, weak_predictions_buffer,
                               shared_status, consumer](OpenBuffer* buffer) {
    auto shared_predictions_buffer = weak_predictions_buffer->lock();
    CHECK_EQ(shared_predictions_buffer.get(), buffer);
    if (editor_state->status()->prompt_buffer() == nullptr) {
      buffer->status()->CopyFrom(*shared_status);
    }
    auto prompt = shared_status->prompt_buffer();
    CHECK(prompt != nullptr);
    predictor(editor_state, prompt->LineAt(LineNumber(0))->ToString(), buffer,
              [shared_status, buffer, consumer, shared_predictions_buffer] {
                buffer->set_current_cursor(LineColumn());
                HandleEndOfFile(buffer, consumer);
              });
  };
  predictions_buffer = std::make_shared<OpenBuffer>(std::move(options));
  *weak_predictions_buffer = predictions_buffer;
  predictions_buffer->Set(buffer_variables::show_in_buffers_list, false);
  predictions_buffer->Set(buffer_variables::allow_dirty_delete, true);
  predictions_buffer->Reload();
  if (editor_state->status()->prompt_buffer() == nullptr) {
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
    for (const auto& search_path : input.search_paths) {
      VLOG(4) << "Considering search path: " << search_path;
      if (!search_path.empty() && !path.empty() && path.front() == '/') {
        VLOG(5) << "Skipping non-empty search path for absolute path.";
        continue;
      }

      wstring path_with_prefix = search_path;
      if (search_path.empty()) {
        path_with_prefix = path.empty() ? L"." : path;
      } else if (!path.empty()) {
        path_with_prefix += L"/" + path;
      }

      string basename_prefix;
      if (path_with_prefix.back() != '/') {
        path_with_prefix = Dirname(path_with_prefix);

        char* path_copy = strdup(ToByteString(path).c_str());
        basename_prefix = basename(path_copy);
        free(path_copy);
      }

      LOG(INFO) << "Reading directory: " << path_with_prefix;

      wstring resolved_path;
      input.resolve_path_options.path = path_with_prefix;
      if (auto results = ResolvePath(input.resolve_path_options);
          results.has_value()) {
        path_with_prefix = results->path;

      } else {
        LOG(INFO) << "Unable to resolve, giving up current search path.";
        continue;
      }

      auto dir = OpenDir(path_with_prefix);
      if (dir == nullptr) {
        LOG(INFO) << "Unable to open, giving up current search path.";
        continue;
      }

      if (path_with_prefix == L".") {
        path_with_prefix = L"";
      } else if (path_with_prefix.back() != L'/') {
        path_with_prefix += L"/";
      }

      struct dirent* entry;
      while ((entry = readdir(dir.get())) != nullptr) {
        string entry_path = entry->d_name;
        if (!std::equal(basename_prefix.begin(), basename_prefix.end(),
                        entry_path.begin()) ||
            entry_path == "." || entry_path == "..") {
          VLOG(6) << "Skipping entry: " << entry_path;
          continue;
        }
        string prediction = ToByteString(path_with_prefix) + entry->d_name +
                            (entry->d_type == DT_DIR ? "/" : "");
        if (!search_path.empty() &&
            std::equal(search_path.begin(), search_path.end(),
                       FromByteString(prediction).begin())) {
          VLOG(6) << "Removing prefix from prediction: " << prediction;
          size_t start = prediction.find_first_not_of('/', search_path.size());
          if (start != prediction.npos) {
            prediction = prediction.substr(start);
          }
        }
        VLOG(5) << "Prediction: " << prediction;
        input.buffer->SchedulePendingWork(
            [buffer = input.buffer,
             line = std::shared_ptr<LazyString>(
                 NewLazyString(FromByteString(prediction)))] {
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
