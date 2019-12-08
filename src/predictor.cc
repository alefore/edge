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

const wchar_t* kLongestPrefixEnvironmentVariable = L"predictor_longest_prefix";
const wchar_t* kLongestDirectoryMatchEnvironmentVariable =
    L"predictor_longest_directory_match";
const wchar_t* kExactMatchEnvironmentVariable = L"predictor_exact_match";

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

  if (auto value = buffer->environment()->Lookup(
          kLongestPrefixEnvironmentVariable, VMType::VM_INTEGER);
      value != nullptr) {
    predict_results.longest_prefix = ColumnNumberDelta(value->integer);
  }

  if (auto value = buffer->environment()->Lookup(
          kLongestDirectoryMatchEnvironmentVariable, VMType::VM_INTEGER);
      value != nullptr) {
    predict_results.longest_directory_match = ColumnNumberDelta(value->integer);
  }

  if (auto value = buffer->environment()->Lookup(kExactMatchEnvironmentVariable,
                                                 VMType::VM_BOOLEAN);
      value != nullptr) {
    predict_results.found_exact_match = value->boolean;
  }

  predict_results.matches = buffer->lines_size().line_delta - 1;
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
  CHECK(prompt_buffer != nullptr);
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
    os << " common_prefix: \"" << lc.common_prefix.value() << "\"";
  }

  os << " matches: " << lc.matches;
  os << " longest_prefix: " << lc.longest_prefix;
  os << " longest_directory_match: " << lc.longest_directory_match;
  os << " found_exact_match: " << lc.found_exact_match;
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
    buffer->environment()->Define(kLongestPrefixEnvironmentVariable,
                                  vm::Value::NewInteger(0));
    buffer->environment()->Define(kLongestDirectoryMatchEnvironmentVariable,
                                  vm::Value::NewInteger(0));
    buffer->environment()->Define(kExactMatchEnvironmentVariable,
                                  vm::Value::NewBool(false));

    auto shared_predictions_buffer = weak_predictions_buffer->lock();
    if (shared_predictions_buffer == nullptr) return;
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

struct DescendDirectoryTreeOutput {
  std::unique_ptr<DIR, std::function<void(DIR*)>> dir;
  // The length of the longest prefix of path that corresponds to a valid
  // directory.
  size_t valid_prefix_length = 0;
  size_t valid_proper_prefix_length = 0;
};

DescendDirectoryTreeOutput DescendDirectoryTree(wstring search_path,
                                                wstring path) {
  DescendDirectoryTreeOutput output;
  VLOG(6) << "Starting search at: " << search_path;
  output.dir = OpenDir(search_path);
  if (output.dir == nullptr) {
    VLOG(5) << "Unable to open search_path: " << search_path;
    return output;
  }

  // We don't use DirectorySplit in order to handle adjacent slashes.
  while (output.valid_prefix_length < path.size()) {
    output.valid_proper_prefix_length = output.valid_prefix_length;
    VLOG(6) << "Iterating at: " << path.substr(0, output.valid_prefix_length);
    auto next_candidate = path.find_first_of(L'/', output.valid_prefix_length);
    if (next_candidate == wstring::npos) {
      next_candidate = path.size();
    } else if (next_candidate == output.valid_prefix_length) {
      ++output.valid_prefix_length;
      continue;
    } else {
      ++next_candidate;
    }
    auto test_path = PathJoin(search_path, path.substr(0, next_candidate));
    VLOG(8) << "Considering: " << test_path;
    auto subdir = OpenDir(test_path);
    if (subdir == nullptr) {
      return output;
    }
    CHECK_GT(next_candidate, output.valid_prefix_length);
    output.dir = std::move(subdir);
    output.valid_prefix_length = next_candidate;
  }
  return output;
}

// Reads the entire contents of `dir`, looking for files that match `pattern`.
// For any files that do, prepends `prefix` and appends them to `buffer`.
void ScanDirectory(DIR* dir, std::wstring pattern, std::wstring prefix,
                   OpenBuffer* buffer) {
  VLOG(5) << "Scanning directory \"" << prefix << "\" looking for: " << pattern;
  // The length of the longest prefix of `pattern` that matches an entry.
  size_t longest_pattern_match = 0;
  struct dirent* entry;
  std::vector<std::shared_ptr<LazyString>> predictions;

  auto FlushPredictions = [&predictions, buffer] {
    buffer->SchedulePendingWork([batch = std::move(predictions), buffer] {
      auto empty_line = std::make_shared<Line>(Line::Options());
      for (auto& prediction : batch) {
        buffer->AppendToLastLine(std::move(prediction));
        buffer->AppendRawLine(empty_line);
      }
    });
    predictions.clear();
  };

  while ((entry = readdir(dir)) != nullptr) {
    string entry_path = entry->d_name;

    auto mismatch_results = std::mismatch(pattern.begin(), pattern.end(),
                                          entry_path.begin(), entry_path.end());
    if (mismatch_results.first != pattern.end()) {
      longest_pattern_match =
          std::max<int>(longest_pattern_match,
                        std::distance(pattern.begin(), mismatch_results.first));
      VLOG(20) << "The entry " << entry_path
               << " doesn't contain the whole prefix. Longest match: "
               << longest_pattern_match;
      continue;
    }
    if (mismatch_results.second == entry_path.end()) {
      buffer->SchedulePendingWork(
          [buffer] { RegisterPredictorExactMatch(buffer); });
    }
    longest_pattern_match = pattern.size();
    predictions.push_back(
        NewLazyString(PathJoin(prefix, FromByteString(entry->d_name)) +
                      (entry->d_type == DT_DIR ? L"/" : L"")));
    if (predictions.size() > 100) {
      FlushPredictions();
    }
  }
  FlushPredictions();
  buffer->SchedulePendingWork([prefix, pattern, longest_pattern_match, buffer,
                               predictions = std::move(predictions)] {
    if (pattern.empty()) {
      RegisterPredictorExactMatch(buffer);
    }
    RegisterPredictorPrefixMatch(prefix.size() + longest_pattern_match, buffer);
  });
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
      auto descend_results = DescendDirectoryTree(search_path, input.path);
      input.buffer->SchedulePendingWork(
          [buffer = input.buffer,
           length = descend_results.valid_proper_prefix_length] {
            RegisterPredictorDirectoryMatch(length, buffer);
          });
      CHECK_LE(descend_results.valid_prefix_length, input.path.size());
      ScanDirectory(descend_results.dir.get(),
                    input.path.substr(descend_results.valid_prefix_length,
                                      input.path.size()),
                    input.path.substr(0, descend_results.valid_prefix_length),
                    input.buffer);
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

void RegisterPredictorPrefixMatch(size_t new_value, OpenBuffer* buffer) {
  auto value = buffer->environment()->Lookup(kLongestPrefixEnvironmentVariable,
                                             VMType::VM_INTEGER);
  if (value == nullptr) return;
  value->integer = std::max(value->integer, static_cast<int>(new_value));
}

void RegisterPredictorDirectoryMatch(size_t new_value, OpenBuffer* buffer) {
  auto value = buffer->environment()->Lookup(
      kLongestDirectoryMatchEnvironmentVariable, VMType::VM_INTEGER);
  if (value == nullptr) return;
  value->integer = std::max(value->integer, static_cast<int>(new_value));
}

void RegisterPredictorExactMatch(OpenBuffer* buffer) {
  auto value = buffer->environment()->Lookup(kExactMatchEnvironmentVariable,
                                             VMType::VM_BOOLEAN);
  if (value == nullptr) return;
  value->boolean = true;
}

}  // namespace afc::editor
