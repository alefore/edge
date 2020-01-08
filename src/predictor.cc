#include "src/predictor.h"

#include <algorithm>
#include <cstring>
#include <iostream>
#include <list>
#include <memory>
#include <regex>
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

void SignalEndOfFile(
    OpenBuffer* buffer,
    futures::DelayedValue<PredictorOutput>::Consumer consumer) {
  buffer->EndOfFile();
  buffer->AddEndOfFileObserver(
      [consumer = std::move(consumer)]() { consumer(PredictorOutput()); });
}

void HandleEndOfFile(std::shared_ptr<OpenBuffer> predictions_buffer,
                     std::function<void(PredictResults)> consumer) {
  CHECK(predictions_buffer != nullptr);

  LOG(INFO) << "Predictions buffer received end of file. Predictions: "
            << predictions_buffer->contents()->size();
  predictions_buffer->SortContents(
      LineNumber(0), predictions_buffer->EndLine(),
      [](const shared_ptr<const Line>& a, const shared_ptr<const Line>& b) {
        return *LowerCase(a->contents()) < *LowerCase(b->contents());
      });

  LOG(INFO) << "Removing duplicates.";
  for (auto line = LineNumber(0);
       line.ToDelta() < predictions_buffer->contents()->size();) {
    if (line == LineNumber(0) ||
        predictions_buffer->LineAt(line.previous())->ToString() !=
            predictions_buffer->LineAt(line)->ToString()) {
      line++;
    } else {
      predictions_buffer->EraseLines(line, line.next());
    }
  }

  wstring common_prefix =
      predictions_buffer->contents()->front()->contents()->ToString();
  PredictResults predict_results;
  if (predictions_buffer->contents()->EveryLine(
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

            auto prefix_end = mismatch(
                common_prefix.begin(), common_prefix.end(), current.begin(),
                [](wchar_t common_c, wchar_t current_c) {
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

  if (auto value = predictions_buffer->environment()->Lookup(
          kLongestPrefixEnvironmentVariable, VMType::VM_INTEGER);
      value != nullptr) {
    predict_results.longest_prefix = ColumnNumberDelta(value->integer);
  }

  if (auto value = predictions_buffer->environment()->Lookup(
          kLongestDirectoryMatchEnvironmentVariable, VMType::VM_INTEGER);
      value != nullptr) {
    predict_results.longest_directory_match = ColumnNumberDelta(value->integer);
  }

  if (auto value = predictions_buffer->environment()->Lookup(
          kExactMatchEnvironmentVariable, VMType::VM_BOOLEAN);
      value != nullptr) {
    predict_results.found_exact_match = value->boolean;
  }

  predict_results.matches = predictions_buffer->lines_size().line_delta - 1;
  predict_results.predictions_buffer = std::move(predictions_buffer);
  consumer(std::move(predict_results));
}

std::wstring GetPredictInput(const PredictOptions& options) {
  auto buffer = options.input_buffer;
  Modifiers modifiers;
  modifiers.direction = Direction::BACKWARDS;
  modifiers.structure = options.input_selection_structure;
  auto range = buffer->FindPartialRange(modifiers, buffer->position());
  range.end = max(range.end, buffer->position());
  auto line = buffer->LineAt(range.begin.line);
  return line
      ->Substring(range.begin.column,
                  range.end.line == range.begin.line
                      ? range.end.column - range.begin.column
                      : line->EndColumn() - range.begin.column)
      ->ToString();
}

// Wrap `consumer` with a consumer that verifies that `prompt_buffer` hasn't
// expired and that its text hasn't changed.
std::function<void(PredictResults)> GuardConsumer(
    const PredictOptions& options, std::wstring initial_text,
    std::function<void(PredictResults)> consumer) {
  return [consumer, options, initial_text](PredictResults predict_results) {
    auto current_text = GetPredictInput(options);
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
  std::shared_ptr<OpenBuffer>& predictions_buffer =
      (*options.editor_state->buffers())[PredictionsBufferName()];
  OpenBuffer::Options buffer_options;
  buffer_options.editor = options.editor_state;
  buffer_options.name = PredictionsBufferName();
  auto weak_predictions_buffer = std::make_shared<std::weak_ptr<OpenBuffer>>();

  auto input = GetPredictInput(options);
  options.callback = GuardConsumer(options, input, std::move(options.callback));
  buffer_options.generate_contents =
      [options, input, weak_predictions_buffer](OpenBuffer* buffer) {
        buffer->environment()->Define(kLongestPrefixEnvironmentVariable,
                                      vm::Value::NewInteger(0));
        buffer->environment()->Define(kLongestDirectoryMatchEnvironmentVariable,
                                      vm::Value::NewInteger(0));
        buffer->environment()->Define(kExactMatchEnvironmentVariable,
                                      vm::Value::NewBool(false));

        auto shared_predictions_buffer = weak_predictions_buffer->lock();
        if (shared_predictions_buffer == nullptr) return;
        CHECK_EQ(shared_predictions_buffer.get(), buffer);

        options
            .predictor({.editor = options.editor_state,
                        .input = std::move(input),
                        .predictions = buffer,
                        .source_buffer = options.source_buffer})
            .SetConsumer([options, shared_predictions_buffer](PredictorOutput) {
              shared_predictions_buffer->set_current_cursor(LineColumn());
              HandleEndOfFile(shared_predictions_buffer, options.callback);
            });
      };
  predictions_buffer = std::make_shared<OpenBuffer>(std::move(buffer_options));
  *weak_predictions_buffer = predictions_buffer;
  predictions_buffer->Set(buffer_variables::show_in_buffers_list, false);
  predictions_buffer->Set(buffer_variables::allow_dirty_delete, true);
  predictions_buffer->Set(buffer_variables::paste_mode, true);
  predictions_buffer->Reload();
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
void ScanDirectory(DIR* dir, const std::wregex& noise_regex,

                   std::wstring pattern, std::wstring prefix,
                   OpenBuffer::LockFunction get_buffer) {
  VLOG(5) << "Scanning directory \"" << prefix << "\" looking for: " << pattern;
  // The length of the longest prefix of `pattern` that matches an entry.
  size_t longest_pattern_match = 0;
  struct dirent* entry;
  std::vector<std::shared_ptr<LazyString>> predictions;

  auto FlushPredictions = [&predictions, get_buffer] {
    get_buffer([batch = std::move(predictions)](OpenBuffer* buffer) {
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
      get_buffer(RegisterPredictorExactMatch);
    }
    longest_pattern_match = pattern.size();
    auto full_path = PathJoin(prefix, FromByteString(entry->d_name)) +
                     (entry->d_type == DT_DIR ? L"/" : L"");
    if (std::regex_match(full_path, noise_regex)) {
      continue;
    }
    predictions.push_back(NewLazyString(std::move(full_path)));
    if (predictions.size() > 100) {
      FlushPredictions();
    }
  }
  FlushPredictions();
  get_buffer([prefix_match = prefix.size() + longest_pattern_match,
              pattern](OpenBuffer* buffer) {
    if (pattern.empty()) {
      RegisterPredictorExactMatch(buffer);
    }
    RegisterPredictorPrefixMatch(prefix_match, buffer);
  });
}

futures::DelayedValue<PredictorOutput> FilePredictor(
    PredictorInput predictor_input) {
  LOG(INFO) << "Generating predictions for: " << predictor_input.input;
  struct AsyncInput {
    OpenBuffer::LockFunction get_buffer;
    wstring path;
    vector<wstring> search_paths;
    ResolvePathOptions resolve_path_options;
    std::wregex noise_regex;
    futures::DelayedValue<PredictorOutput>::Consumer output_consumer;
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
      input.get_buffer([length = descend_results.valid_proper_prefix_length](
                           OpenBuffer* buffer) {
        RegisterPredictorDirectoryMatch(length, buffer);
      });
      CHECK_LE(descend_results.valid_prefix_length, input.path.size());
      ScanDirectory(descend_results.dir.get(), input.noise_regex,
                    input.path.substr(descend_results.valid_prefix_length,
                                      input.path.size()),
                    input.path.substr(0, descend_results.valid_prefix_length),
                    input.get_buffer);
    }
    input.get_buffer([output_consumer = std::move(input.output_consumer)](
                         OpenBuffer* buffer) {
      LOG(INFO) << "Signaling end of file.";
      SignalEndOfFile(buffer, std::move(output_consumer));
    });
    return 0;
  };
  static AsyncProcessor<AsyncInput, int> async_processor(std::move(options));

  futures::Future<PredictorOutput> future;
  AsyncInput input{
      .get_buffer = predictor_input.predictions->GetLockFunction(),
      .path = predictor_input.editor->expand_path(predictor_input.input),
      .search_paths = {},
      .resolve_path_options = ResolvePathOptions::New(predictor_input.editor),
      .noise_regex = predictor_input.source_buffer != nullptr
                         ? std::wregex(predictor_input.source_buffer->Read(
                               buffer_variables::directory_noise))
                         : std::wregex(),
      .output_consumer = future.consumer()};
  GetSearchPaths(predictor_input.editor, &input.search_paths);
  async_processor.Push(std::move(input));
  return future.value();
}

futures::DelayedValue<PredictorOutput> EmptyPredictor(PredictorInput input) {
  futures::Future<PredictorOutput> future;
  SignalEndOfFile(input.predictions, future.consumer());
  return future.value();
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
  return [contents](PredictorInput input) {
    for (auto it = contents->lower_bound(input.input); it != contents->end();
         ++it) {
      auto result =
          mismatch(input.input.begin(), input.input.end(), (*it).first.begin());
      if (result.first == input.input.end()) {
        input.predictions->AppendToLastLine(it->second);
        input.predictions->AppendRawLine(
            std::make_shared<Line>(Line::Options()));
      } else {
        break;
      }
    }
    futures::Future<PredictorOutput> future;
    SignalEndOfFile(input.predictions, future.consumer());
    return future.value();
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

Predictor DictionaryPredictor(std::shared_ptr<const OpenBuffer> dictionary) {
  return [dictionary](PredictorInput input) {
    auto contents = dictionary->contents();
    auto input_line =
        std::make_shared<const Line>(Line::Options(NewLazyString(input.input)));

    LineNumber line = contents->upper_bound(
        input_line,
        [](const shared_ptr<const Line>& a, const shared_ptr<const Line>& b) {
          return a->ToString() < b->ToString();
        });

    // TODO: This has complexity N log N. We could instead extend BufferContents
    // to expose a wrapper around `Suffix`, allowing this to have complexity N
    // (just take the suffix once, and then walk it, with `ConstTree::Every`).
    while (line < contents->EndLine()) {
      auto line_contents = contents->at(line);
      auto line_str = line_contents->ToString();
      auto result =
          mismatch(input.input.begin(), input.input.end(), line_str.begin());
      if (result.first != input.input.end()) {
        break;
      }
      input.predictions->AppendToLastLine(*line_contents);
      input.predictions->AppendRawLine(std::make_shared<Line>(Line::Options()));
    }
    futures::Future<PredictorOutput> future;
    SignalEndOfFile(input.predictions, future.consumer());
    return future.value();
  };
}

}  // namespace afc::editor
