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
#include "src/editor.h"
#include "src/file_link_mode.h"
#include "src/futures/delete_notification.h"
#include "src/infrastructure/dirname.h"
#include "src/language/lazy_string/char_buffer.h"
#include "src/language/lazy_string/lowercase.h"
#include "src/language/overload.h"
#include "src/language/wstring.h"
#include "src/predictor.h"
#include "src/tests/tests.h"

namespace afc::editor {
namespace {
using concurrent::WorkQueue;
using concurrent::WorkQueueChannelConsumeMode;
using futures::DeleteNotification;
using infrastructure::FileSystemDriver;
using infrastructure::OpenDir;
using infrastructure::Path;
using infrastructure::PathJoin;
using infrastructure::Tracker;
using language::EmptyValue;
using language::Error;
using language::FromByteString;
using language::IgnoreErrors;
using language::MakeNonNullShared;
using language::NonNull;
using language::overload;
using language::Success;
using language::lazy_string::ColumnNumber;
using language::lazy_string::ColumnNumberDelta;
using language::lazy_string::LazyString;
using language::lazy_string::NewLazyString;

namespace gc = language::gc;

PredictResults BuildResults(OpenBuffer& predictions_buffer,
                            PredictorOutput predictor_output) {
  LOG(INFO) << "Predictions buffer received end of file. Predictions: "
            << predictions_buffer.contents().size();
  if (predictions_buffer.lines_size() > LineNumberDelta(1))
    predictions_buffer.SortContents(
        LineNumber(0), predictions_buffer.EndLine() - LineNumberDelta(1),
        [](const NonNull<std::shared_ptr<const Line>>& a,
           const NonNull<std::shared_ptr<const Line>>& b) {
          return LowerCase(a->contents()).value() <
                 LowerCase(b->contents()).value();
        });

  LOG(INFO) << "Removing duplicates.";
  for (auto line = LineNumber(1);
       line.ToDelta() < predictions_buffer.contents().size();) {
    if (predictions_buffer.contents().at(line.previous())->ToString() !=
        predictions_buffer.contents().at(line)->ToString()) {
      line++;
    } else {
      predictions_buffer.EraseLines(line, line.next());
    }
  }

  std::optional<std::wstring> common_prefix;
  predictions_buffer.contents().EveryLine([&common_prefix](LineNumber,
                                                           const Line& line) {
    if (line.empty()) {
      return true;
    }
    VLOG(5) << "Considering prediction: " << line.ToString()
            << " (end column: " << line.EndColumn() << ")";
    if (!common_prefix.has_value()) {
      common_prefix = line.ToString();
      return true;
    }

    ColumnNumberDelta current_size =
        std::min(ColumnNumberDelta(common_prefix.value().size()),
                 line.EndColumn().ToDelta());
    std::wstring current =
        line.Substring(ColumnNumber(0), current_size)->ToString();

    auto prefix_end =
        mismatch(common_prefix->begin(), common_prefix->end(), current.begin(),
                 [](wchar_t common_c, wchar_t current_c) {
                   return towlower(common_c) == towlower(current_c);
                 });
    if (prefix_end.first != common_prefix->end()) {
      if (prefix_end.first == common_prefix->begin()) {
        LOG(INFO) << "Aborting completion.";
        common_prefix = L"";
        return false;
      }
      common_prefix = std::wstring(common_prefix->begin(), prefix_end.first);
    }
    return true;
  });
  return PredictResults{
      .common_prefix = common_prefix,
      .predictions_buffer = predictions_buffer.NewRoot(),
      .matches = (predictions_buffer.lines_size() - LineNumberDelta(1)).read(),
      .predictor_output = predictor_output};
}

std::wstring GetPredictInput(const PredictOptions& options) {
  if (options.text.has_value()) return options.text.value();
  std::optional<gc::Root<OpenBuffer>> buffer = options.input_buffer;
  // TODO(2022-05-16): Why is this CHECK safe?
  CHECK(buffer.has_value());
  Modifiers modifiers;
  modifiers.direction = Direction::kBackwards;
  modifiers.structure = options.input_selection_structure;
  auto range =
      buffer->ptr()->FindPartialRange(modifiers, buffer->ptr()->position());
  range.end = std::max(range.end, buffer->ptr()->position());
  auto line = buffer->ptr()->LineAt(range.begin.line);
  CHECK_LE(range.begin.column, line->EndColumn());
  if (range.begin.line == range.end.line) {
    CHECK_GE(range.end.column, range.begin.column);
    range.end.column = std::min(range.end.column, line->EndColumn());
  } else {
    CHECK_GE(line->EndColumn(), range.begin.column);
  }
  return line
      ->Substring(range.begin.column,
                  (range.begin.line == range.end.line ? range.end.column
                                                      : line->EndColumn()) -
                      range.begin.column)
      ->ToString();
}
}  // namespace

std::ostream& operator<<(std::ostream& os, const PredictorOutput& lc) {
  os << "[PredictorOutput longest_prefix: " << lc.longest_prefix;
  os << " longest_directory_match: " << lc.longest_directory_match;
  os << " found_exact_match: " << lc.found_exact_match << "]";
  return os;
}

std::ostream& operator<<(std::ostream& os, const PredictResults& lc) {
  os << "[PredictResults";
  if (lc.common_prefix.has_value()) {
    os << " common_prefix: \"" << lc.common_prefix.value() << "\"";
  }

  os << " matches: " << lc.matches;
  os << " predictor_output: " << lc.predictor_output;
  os << "]";
  return os;
}

futures::Value<std::optional<PredictResults>> Predict(PredictOptions options) {
  auto shared_options = std::make_shared<PredictOptions>(std::move(options));
  futures::Future<std::optional<PredictResults>> output;
  OpenBuffer::Options buffer_options{.editor = shared_options->editor_state,
                                     .name = PredictionsBufferName()};

  if (shared_options->progress_channel == nullptr) {
    shared_options->progress_channel = std::make_unique<ProgressChannel>(
        shared_options->editor_state.work_queue(), [](ProgressInformation) {},
        WorkQueueChannelConsumeMode::kLastAvailable);
  }
  CHECK(!shared_options->abort_value.has_value());

  auto input = GetPredictInput(*shared_options);
  buffer_options.generate_contents =
      [shared_options = std::move(shared_options), input,
       consumer = std::move(output.consumer)](OpenBuffer& buffer) {
        CHECK(shared_options->progress_channel != nullptr);
        return shared_options
            ->predictor({.editor = shared_options->editor_state,
                         .input = std::move(input),
                         .predictions = buffer,
                         .source_buffers = shared_options->source_buffers,
                         .progress_channel = *shared_options->progress_channel,
                         .abort_value = shared_options->abort_value})
            .Transform([shared_options, input, &buffer,
                        consumer](PredictorOutput predictor_output) {
              shared_options->progress_channel = nullptr;
              buffer.set_current_cursor(LineColumn());
              auto results = BuildResults(buffer, predictor_output);
              consumer(GetPredictInput(*shared_options) == input &&
                               !shared_options->abort_value.has_value()
                           ? std::optional<PredictResults>(results)
                           : std::nullopt);
              return Success();
            });
      };
  auto predictions_buffer = OpenBuffer::New(std::move(buffer_options));
  predictions_buffer.ptr()->Set(buffer_variables::show_in_buffers_list, false);
  predictions_buffer.ptr()->Set(buffer_variables::allow_dirty_delete, true);
  predictions_buffer.ptr()->Set(buffer_variables::paste_mode, true);
  predictions_buffer.ptr()->Reload();
  return std::move(output.value);
}

struct DescendDirectoryTreeOutput {
  std::unique_ptr<DIR, std::function<void(DIR*)>> dir;
  // The length of the longest prefix of path that corresponds to a valid
  // directory.
  size_t valid_prefix_length = 0;
  size_t valid_proper_prefix_length = 0;
};

// TODO(easy): Receive Path rather than std::wstrings.
DescendDirectoryTreeOutput DescendDirectoryTree(Path search_path,
                                                std::wstring path) {
  DescendDirectoryTreeOutput output;
  VLOG(6) << "Starting search at: " << search_path;
  output.dir = OpenDir(search_path.read());
  if (output.dir == nullptr) {
    VLOG(5) << "Unable to open search_path: " << search_path;
    return output;
  }

  // We don't use DirectorySplit in order to handle adjacent slashes.
  while (output.valid_prefix_length < path.size()) {
    output.valid_proper_prefix_length = output.valid_prefix_length;
    VLOG(6) << "Iterating at: " << path.substr(0, output.valid_prefix_length);
    auto next_candidate = path.find_first_of(L'/', output.valid_prefix_length);
    if (next_candidate == std::wstring::npos) {
      next_candidate = path.size();
    } else if (next_candidate == output.valid_prefix_length) {
      ++output.valid_prefix_length;
      continue;
    } else {
      ++next_candidate;
    }
    auto test_path =
        PathJoin(search_path.read(), path.substr(0, next_candidate));
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
                   std::wstring pattern, std::wstring prefix, int* matches,
                   ProgressChannel& progress_channel,
                   DeleteNotification::Value& abort_value,
                   OpenBuffer::LockFunction get_buffer,
                   concurrent::Protected<PredictorOutput>& predictor_output) {
  static Tracker top_tracker(L"FilePredictor::ScanDirectory");
  auto top_call = top_tracker.Call();

  CHECK(dir != nullptr);
  VLOG(5) << "Scanning directory \"" << prefix << "\" looking for: " << pattern;
  // The length of the longest prefix of `pattern` that matches an entry.
  size_t longest_pattern_match = 0;
  struct dirent* entry;
  std::vector<NonNull<std::shared_ptr<Line>>> predictions;

  auto FlushPredictions = [&predictions, get_buffer] {
    get_buffer([batch = std::move(predictions)](OpenBuffer& buffer) mutable {
      static Tracker tracker(L"FilePredictor::ScanDirectory::FlushPredictions");
      auto call = tracker.Call();
      for (NonNull<std::shared_ptr<Line>>& prediction : batch) {
        buffer.AppendToLastLine(std::move(prediction.value()));
        buffer.AppendRawLine(NonNull<std::shared_ptr<Line>>());
      }
    });
    predictions.clear();
  };

  while ((entry = readdir(dir)) != nullptr) {
    if (abort_value.has_value()) return;
    std::string entry_path = entry->d_name;
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
      predictor_output.lock(
          [](PredictorOutput& output) { output.found_exact_match = true; });
    }
    longest_pattern_match = pattern.size();
    auto full_path = PathJoin(prefix, FromByteString(entry->d_name)) +
                     (entry->d_type == DT_DIR ? L"/" : L"");
    if (std::regex_match(full_path, noise_regex)) {
      continue;
    }
    predictions.push_back(MakeNonNullShared<Line>(
        Line::Options(NewLazyString(std::move(full_path)))));
    if (predictions.size() > 100) {
      FlushPredictions();
    }
    ++*matches;
    progress_channel.Push(ProgressInformation{
        .values = {{StatusPromptExtraInformationKey(L"files"),
                    std::to_wstring(*matches)}}});
  }
  FlushPredictions();
  progress_channel.Push(
      ProgressInformation{.values = {{StatusPromptExtraInformationKey(L"files"),
                                      std::to_wstring(*matches)}}});

  predictor_output.lock([&](PredictorOutput& output) {
    output.longest_prefix =
        std::max(output.longest_prefix,
                 ColumnNumberDelta(prefix.size() + longest_pattern_match));
    if (pattern.empty()) {
      output.found_exact_match = true;
    }
  });
}

futures::Value<PredictorOutput> FilePredictor(PredictorInput predictor_input) {
  LOG(INFO) << "Generating predictions for: " << predictor_input.input;
  auto search_paths = std::make_shared<std::vector<Path>>();
  // TODO(easy, 2022-12-11, non-copyable-function): Change to MakeNonNullUnique.
  auto predictor_output =
      MakeNonNullShared<concurrent::Protected<PredictorOutput>>();
  return GetSearchPaths(predictor_input.editor, search_paths.get())
      .Transform([predictor_input, search_paths,
                  &predictor_output = predictor_output.value()](EmptyValue) {
        // We can't use a Path type because this comes from the prompt and ...
        // may not actually be a valid path.
        std::wstring path_input = std::visit(
            overload{[&](Error) { return predictor_input.input; },
                     [&](Path path) {
                       return predictor_input.editor.expand_path(path).read();
                     }},
            Path::FromString(predictor_input.input));

        OpenBuffer::LockFunction get_buffer =
            predictor_input.predictions.GetLockFunction();
        ResolvePathOptions resolve_path_options =
            ResolvePathOptions<EmptyValue>::New(
                predictor_input.editor,
                std::make_shared<FileSystemDriver>(
                    predictor_input.editor.thread_pool()));

        // TODO: Don't use sources_buffers[0], ignoring the other buffers.
        std::wregex noise_regex =
            predictor_input.source_buffers.empty()
                ? std::wregex()
                : std::wregex(predictor_input.source_buffers[0].ptr()->Read(
                      buffer_variables::directory_noise));
        predictor_input.editor.thread_pool().RunIgnoringResult(std::bind_front(
            [search_paths, &predictor_output, path_input, get_buffer,
             resolve_path_options,
             noise_regex](ProgressChannel& progress_channel,
                          DeleteNotification::Value abort_value) {
              if (!path_input.empty() && *path_input.begin() == L'/') {
                *search_paths = {Path::Root()};
              } else {
                std::vector<Path> resolved_paths;
                for (Path& search_path : *search_paths) {
                  std::visit(overload{IgnoreErrors{},
                                      [&](Path path) {
                                        resolved_paths.push_back(path);
                                      }},
                             search_path.Resolve());
                }
                *search_paths = std::move(resolved_paths);

                std::set<Path> unique_paths_set;
                std::vector<Path> unique_paths;
                for (const auto& search_path : *search_paths) {
                  if (unique_paths_set.insert(search_path).second) {
                    unique_paths.push_back(search_path);
                  }
                }

                *search_paths = std::move(unique_paths);
              }

              int matches = 0;
              for (const auto& search_path : *search_paths) {
                VLOG(4) << "Considering search path: " << search_path;
                DescendDirectoryTreeOutput descend_results =
                    DescendDirectoryTree(search_path, path_input);
                if (descend_results.dir == nullptr) {
                  LOG(WARNING) << "Unable to descend: " << search_path;
                  continue;
                }
                predictor_output.lock(
                    [&descend_results](PredictorOutput& output) {
                      output.longest_directory_match = std::max(
                          output.longest_directory_match,
                          ColumnNumberDelta(
                              descend_results.valid_proper_prefix_length));
                    });
                CHECK_LE(descend_results.valid_prefix_length,
                         path_input.size());
                ScanDirectory(
                    descend_results.dir.get(), noise_regex,
                    path_input.substr(descend_results.valid_prefix_length,
                                      path_input.size()),
                    path_input.substr(0, descend_results.valid_prefix_length),
                    &matches, progress_channel, abort_value, get_buffer,
                    predictor_output);
                if (abort_value.has_value()) return;
              }
              get_buffer([](OpenBuffer& buffer) {
                LOG(INFO) << "Signaling end of file.";
                buffer.EndOfFile();
              });
            },
            predictor_input.progress_channel,
            std::move(predictor_input.abort_value)));
        return predictor_input.predictions.WaitForEndOfFile();
      })
      .Transform([predictor_output](EmptyValue) {
        return predictor_output->lock(
            [](PredictorOutput& output) -> PredictorOutput { return output; });
      });
}

futures::Value<PredictorOutput> EmptyPredictor(PredictorInput input) {
  input.predictions.EndOfFile();
  return futures::Past(PredictorOutput());
}

namespace {
void RegisterVariations(const std::wstring& prediction, wchar_t separator,
                        std::vector<std::wstring>* output) {
  size_t start = 0;
  DVLOG(5) << "Generating predictions for: " << prediction;
  while (true) {
    start = prediction.find_first_not_of(separator, start);
    if (start == std::wstring::npos) {
      return;
    }
    output->push_back(prediction.substr(start));
    DVLOG(6) << "Prediction: " << output->back();
    start = prediction.find_first_of(separator, start);
    if (start == std::wstring::npos) {
      return;
    }
  }
}

}  // namespace

const BufferName& PredictionsBufferName() {
  static const BufferName* const value = new BufferName(L"- predictions");
  return *value;
}

Predictor PrecomputedPredictor(const std::vector<std::wstring>& predictions,
                               wchar_t separator) {
  const auto contents = std::make_shared<
      std::multimap<std::wstring, NonNull<std::shared_ptr<LazyString>>>>();
  for (const std::wstring& prediction_raw : predictions) {
    std::vector<std::wstring> variations;
    RegisterVariations(prediction_raw, separator, &variations);
    const NonNull<std::shared_ptr<LazyString>> prediction =
        NewLazyString(prediction_raw);
    for (auto& variation : variations) {
      contents->insert(make_pair(variation, prediction));
    }
  }
  return [contents](PredictorInput input) {
    for (auto it = contents->lower_bound(input.input); it != contents->end();
         ++it) {
      auto result =
          mismatch(input.input.begin(), input.input.end(), (*it).first.begin());
      if (result.first == input.input.end()) {
        input.predictions.AppendToLastLine(it->second);
        input.predictions.AppendRawLine(NonNull<std::shared_ptr<Line>>());
      } else {
        break;
      }
    }
    input.progress_channel.Push(ProgressInformation{
        .values = {
            {StatusPromptExtraInformationKey(L"values"),
             std::to_wstring(input.predictions.lines_size().read() - 1)}}});
    input.predictions.EndOfFile();
    return futures::Past(PredictorOutput());
  };
}

namespace {
const bool buffer_tests_registration =
    tests::Register(L"PrecomputedPredictor", [] {
      const static Predictor test_predictor = PrecomputedPredictor(
          {L"foo", L"bar", L"bard", L"foo_bar", L"alejo"}, L'_');
      auto predict = [&](std::wstring input) {
        ProgressChannel channel(
            WorkQueue::New(), [](ProgressInformation) {},
            WorkQueueChannelConsumeMode::kAll);
        gc::Root<OpenBuffer> buffer = NewBufferForTests();
        test_predictor(PredictorInput{.editor = buffer.ptr()->editor(),
                                      .input = input,
                                      .predictions = buffer.ptr().value(),
                                      .source_buffers = {},
                                      .progress_channel = channel});
        buffer.ptr()->SortContents(
            LineNumber(), buffer.ptr()->EndLine(),
            [](const NonNull<std::shared_ptr<const Line>>& a,
               const NonNull<std::shared_ptr<const Line>>& b) {
              return a->ToString() < b->ToString();
            });
        VLOG(5) << "Contents: " << buffer.ptr()->contents().ToString();
        return buffer.ptr()->contents().ToString();
      };
      auto test_predict = [&](std::wstring input,
                              std::function<void(PredictResults)> callback) {
        gc::Root<OpenBuffer> buffer = NewBufferForTests();
        bool executed = false;
        Predict(PredictOptions{.editor_state = buffer.ptr()->editor(),
                               .predictor = test_predictor,
                               .text = input,
                               .source_buffers = {}})
            .Transform([&](std::optional<PredictResults> predict_results) {
              CHECK(!std::exchange(executed, true));
              callback(predict_results.value());
              return futures::Past(EmptyValue());
            });
        CHECK(executed);
      };

      return std::vector<tests::Test>(
          {{.name = L"CallNoPredictions",
            .callback = [&] { CHECK(predict(L"quux") == L""); }},
           {.name = L"CallNoPredictionsLateOverlap",
            .callback = [&] { CHECK(predict(L"o") == L""); }},
           {.name = L"CallExactPrediction",
            .callback = [&] { CHECK(predict(L"ale") == L"alejo\n"); }},
           {.name = L"CallTokenPrediction",
            .callback =
                [&] { CHECK(predict(L"bar") == L"bar\nbard\nfoo_bar\n"); }},
           {.name = L"NoMatchesCheckOutput",
            .callback =
                [&] {
                  test_predict(L"xxx", [&](PredictResults output) {
                    CHECK_EQ(output.matches, 0);
                    CHECK(!output.common_prefix.has_value());
                  });
                }},
           {.name = L"SingleMatchCheckOutput",
            .callback =
                [&] {
                  test_predict(L"alej", [&](PredictResults output) {
                    CHECK_EQ(output.matches, 1);
                    CHECK(output.common_prefix == L"alejo");
                  });
                }},
           {.name = L"TwoMatchesCheckOutput", .callback = [&] {
              test_predict(L"fo", [&](PredictResults output) {
                CHECK_EQ(output.matches, 2);
                CHECK(output.common_prefix.value() == L"foo");
              });
            }}});
    }());
}  // namespace

Predictor DictionaryPredictor(gc::Root<const OpenBuffer> dictionary_root) {
  return [dictionary_root](PredictorInput input) {
    const OpenBuffer& dictionary = dictionary_root.ptr().value();
    const BufferContents& contents = dictionary.contents();
    auto input_line = MakeNonNullShared<const Line>(
        Line::Options(NewLazyString(input.input)));

    LineNumber line = contents.upper_bound(
        input_line, [](const NonNull<std::shared_ptr<const Line>>& a,
                       const NonNull<std::shared_ptr<const Line>>& b) {
          return a->ToString() < b->ToString();
        });

    // TODO: This has complexity N log N. We could instead extend BufferContents
    // to expose a wrapper around `Suffix`, allowing this to have complexity N
    // (just take the suffix once, and then walk it, with `ConstTree::Every`).
    while (line < contents.EndLine()) {
      auto line_contents = contents.at(line);
      auto line_str = line_contents->ToString();
      auto result =
          mismatch(input.input.begin(), input.input.end(), line_str.begin());
      if (result.first != input.input.end()) {
        break;
      }
      input.predictions.AppendRawLine(line_contents->contents());

      ++line;
    }

    input.predictions.EndOfFile();
    return futures::Past(PredictorOutput());
  };
}

void RegisterLeaves(const OpenBuffer& buffer, const ParseTree& tree,
                    std::set<std::wstring>* words) {
  DCHECK(words != nullptr);
  if (tree.children().empty() &&
      tree.range().begin.line == tree.range().end.line) {
    CHECK_LE(tree.range().begin.column, tree.range().end.column);
    auto line = buffer.LineAt(tree.range().begin.line);
    auto word =
        line->Substring(tree.range().begin.column,
                        std::min(tree.range().end.column, line->EndColumn()) -
                            tree.range().begin.column)
            ->ToString();
    if (!word.empty()) {
      DVLOG(5) << "Found leave: " << word;
      words->insert(word);
    }
  }
  for (auto& child : tree.children()) {
    RegisterLeaves(buffer, child, words);
  }
}

futures::Value<PredictorOutput> SyntaxBasedPredictor(PredictorInput input) {
  if (input.source_buffers.empty()) return futures::Past(PredictorOutput());
  std::set<std::wstring> words;
  for (gc::Root<OpenBuffer>& buffer : input.source_buffers) {
    RegisterLeaves(buffer.ptr().value(), buffer.ptr()->parse_tree().value(),
                   &words);
    std::wistringstream keywords(
        buffer.ptr()->Read(buffer_variables::language_keywords));
    words.insert(std::istream_iterator<std::wstring, wchar_t>(keywords),
                 std::istream_iterator<std::wstring, wchar_t>());
  }
  gc::Root<OpenBuffer> dictionary = OpenBuffer::New(
      {.editor = input.editor, .name = BufferName(L"Dictionary")});
  for (auto& word : words) {
    dictionary.ptr()->AppendLine(NewLazyString(std::move(word)));
  }
  return DictionaryPredictor(gc::Root<const OpenBuffer>(std::move(dictionary)))(
      input);
}
}  // namespace afc::editor
