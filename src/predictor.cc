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
#include "src/language/error/view.h"
#include "src/language/gc_view.h"
#include "src/language/lazy_string/char_buffer.h"
#include "src/language/lazy_string/lowercase.h"
#include "src/language/overload.h"
#include "src/language/text/sorted_line_sequence.h"
#include "src/language/wstring.h"
#include "src/predictor.h"
#include "src/structure.h"
#include "src/tests/tests.h"

namespace gc = afc::language::gc;
namespace container = afc::language::container;

using afc::concurrent::ChannelAll;
using afc::concurrent::VersionPropertyKey;
using afc::concurrent::WorkQueue;
using afc::futures::DeleteNotification;
using afc::infrastructure::FileSystemDriver;
using afc::infrastructure::OpenDir;
using afc::infrastructure::Path;
using afc::infrastructure::PathJoin;
using afc::infrastructure::Tracker;
using afc::language::EmptyValue;
using afc::language::Error;
using afc::language::FromByteString;
using afc::language::IgnoreErrors;
using afc::language::MakeNonNullShared;
using afc::language::NonNull;
using afc::language::OptionalFrom;
using afc::language::overload;
using afc::language::PossibleError;
using afc::language::Success;
using afc::language::ValueOrError;
using afc::language::lazy_string::ColumnNumber;
using afc::language::lazy_string::ColumnNumberDelta;
using afc::language::lazy_string::LazyString;
using afc::language::lazy_string::NewLazyString;
using afc::language::text::Line;
using afc::language::text::LineBuilder;
using afc::language::text::LineColumn;
using afc::language::text::LineNumber;
using afc::language::text::LineNumberDelta;
using afc::language::text::LineSequence;
using afc::language::text::MutableLineSequence;
using afc::language::text::SortedLineSequence;
using afc::language::text::SortedLineSequenceUniqueLines;
using afc::language::view::SkipErrors;

namespace afc::editor {
namespace {
ValueOrError<PredictResults> BuildResults(
    OpenBuffer& predictions_buffer, PredictorOutput predictor_output,
    DeleteNotification::Value& abort_value) {
  TRACK_OPERATION(Predictor_BuildResults);
  LOG(INFO) << "Predictions buffer received end of file. Predictions: "
            << predictions_buffer.contents().size();
  if (abort_value.has_value()) return Error(L"Aborted");

  std::optional<std::wstring> common_prefix;
  predictor_output.contents.sorted_lines().lines().EveryLine(
      [&common_prefix, &abort_value](
          LineNumber,
          const language::NonNull<std::shared_ptr<const Line>>& line) {
        if (abort_value.has_value()) return false;
        if (line->empty()) {
          return true;
        }
        VLOG(5) << "Considering prediction: " << line->ToString()
                << " (end column: " << line->EndColumn() << ")";
        if (!common_prefix.has_value()) {
          common_prefix = line->ToString();
          return true;
        }

        ColumnNumberDelta current_size =
            std::min(ColumnNumberDelta(common_prefix.value().size()),
                     line->EndColumn().ToDelta());
        std::wstring current =
            line->Substring(ColumnNumber(0), current_size)->ToString();

        auto prefix_end =
            mismatch(common_prefix->begin(), common_prefix->end(),
                     current.begin(), [](wchar_t common_c, wchar_t current_c) {
                       return towlower(common_c) == towlower(current_c);
                     });
        if (prefix_end.first != common_prefix->end()) {
          if (prefix_end.first == common_prefix->begin()) {
            LOG(INFO) << "Aborting completion.";
            common_prefix = L"";
            return false;
          }
          common_prefix =
              std::wstring(common_prefix->begin(), prefix_end.first);
        }
        return true;
      });
  if (abort_value.has_value()) return Error(L"Aborted");
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
  modifiers.structure = Structure::kLine;
  auto range =
      buffer->ptr()->FindPartialRange(modifiers, buffer->ptr()->position());
  range.set_end(std::max(range.end(), buffer->ptr()->position()));
  auto line = buffer->ptr()->LineAt(range.begin().line);
  CHECK_LE(range.begin().column, line->EndColumn());
  if (range.begin().line == range.end().line) {
    CHECK_GE(range.end().column, range.begin().column);
    range.set_end_column(std::min(range.end().column, line->EndColumn()));
  } else {
    CHECK_GE(line->EndColumn(), range.begin().column);
  }
  return line
      ->Substring(range.begin().column,
                  (range.begin().line == range.end().line ? range.end().column
                                                          : line->EndColumn()) -
                      range.begin().column)
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
    shared_options->progress_channel =
        std::make_unique<ChannelAll<ProgressInformation>>(
            [](ProgressInformation) {});
  }
  CHECK(!shared_options->abort_value.has_value());

  NonNull<std::shared_ptr<LazyString>> input =
      NewLazyString(GetPredictInput(*shared_options));

  auto predictions_buffer = OpenBuffer::New(std::move(buffer_options));
  predictions_buffer.ptr()->Set(buffer_variables::show_in_buffers_list, false);
  predictions_buffer.ptr()->Set(buffer_variables::allow_dirty_delete, true);
  predictions_buffer.ptr()->Set(buffer_variables::paste_mode, true);

  return shared_options
      ->predictor(
          PredictorInput{.editor = shared_options->editor_state,
                         .input = input,
                         .predictions = predictions_buffer.ptr().value(),
                         .source_buffers = shared_options->source_buffers,
                         .progress_channel = *shared_options->progress_channel,
                         .abort_value = shared_options->abort_value})
      .Transform([shared_options, input,
                  predictions_buffer](PredictorOutput predictor_output) mutable
                 -> ValueOrError<PredictResults> {
        shared_options->progress_channel = nullptr;
        predictions_buffer.ptr()->set_current_cursor(LineColumn());
        DECLARE_OR_RETURN(
            auto results,
            BuildResults(predictions_buffer.ptr().value(), predictor_output,
                         shared_options->abort_value));
        if (NewLazyString(GetPredictInput(*shared_options)).value() !=
                input.value() ||
            shared_options->abort_value.has_value())
          return Error(L"Aborted");
        return results;
      })
      .Transform(
          [](PredictResults r) -> ValueOrError<std::optional<PredictResults>> {
            return r;
          })
      .ConsumeErrors(
          [](auto) { return futures::Past(std::optional<PredictResults>()); });
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
                   MutableLineSequence& output_lines,
                   PredictorOutput& predictor_output) {
  static Tracker top_tracker(L"FilePredictor::ScanDirectory");
  auto top_call = top_tracker.Call();

  CHECK(dir != nullptr);
  VLOG(5) << "Scanning directory \"" << prefix << "\" looking for: " << pattern;
  // The length of the longest prefix of `pattern` that matches an entry.
  size_t longest_pattern_match = 0;
  struct dirent* entry;

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
      predictor_output.found_exact_match = true;
    }
    longest_pattern_match = pattern.size();
    auto full_path = PathJoin(prefix, FromByteString(entry->d_name)) +
                     (entry->d_type == DT_DIR ? L"/" : L"");
    if (std::regex_match(full_path, noise_regex)) {
      continue;
    }
    output_lines.push_back(
        MakeNonNullShared<Line>(
            LineBuilder(NewLazyString(std::move(full_path))).Build()),
        MutableLineSequence::ObserverBehavior::kHide);
    ++*matches;
    if (*matches % 100 == 0)
      progress_channel.Push(
          ProgressInformation{.values = {{VersionPropertyKey(L"files"),
                                          std::to_wstring(*matches)}}});
  }

  progress_channel.Push(ProgressInformation{
      .values = {{VersionPropertyKey(L"files"), std::to_wstring(*matches)}}});

  predictor_output.longest_prefix =
      std::max(predictor_output.longest_prefix,
               ColumnNumberDelta(prefix.size() + longest_pattern_match));
  if (pattern.empty()) {
    predictor_output.found_exact_match = true;
  }
}

futures::Value<PredictorOutput> FilePredictor(PredictorInput predictor_input) {
  LOG(INFO) << "Generating predictions for: "
            << predictor_input.input->ToString();
  return GetSearchPaths(predictor_input.editor)
      .Transform([predictor_input](std::vector<Path> search_paths) {
        // We can't use a Path type because this comes from the prompt and ...
        // may not actually be a valid path.
        std::wstring path_input = std::visit(
            overload{[&](Error) {
                       // TODO(easy, 2023-12-02): Get rid of ToString.
                       return predictor_input.input->ToString();
                     },
                     [&](Path path) {
                       return predictor_input.editor.expand_path(path).read();
                     }},
            Path::FromString(predictor_input.input));

        // TODO: Don't use sources_buffers[0], ignoring the other buffers.
        std::wregex noise_regex =
            predictor_input.source_buffers.empty()
                ? std::wregex()
                : std::wregex(predictor_input.source_buffers[0].ptr()->Read(
                      buffer_variables::directory_noise));
        return predictor_input.editor.thread_pool().Run(std::bind_front(
            [path_input, search_paths, noise_regex](
                ProgressChannel& progress_channel,
                DeleteNotification::Value abort_value) mutable {
              if (!path_input.empty() && *path_input.begin() == L'/') {
                search_paths = {Path::Root()};
              } else {
                search_paths = container::MaterializeVector(
                    search_paths | std::views::transform([](Path path) {
                      return std::visit(
                          overload{[](infrastructure::AbsolutePath output)
                                       -> ValueOrError<Path> { return output; },
                                   [](Error error) {
                                     return ValueOrError<Path>(error);
                                   }},
                          path.Resolve());
                    }) |
                    SkipErrors);

                std::set<Path> already_seen;
                search_paths = container::MaterializeVector(
                    std::move(search_paths) |
                    std::views::filter([&already_seen](const Path& path) {
                      return already_seen.insert(path).second;
                    }));
              }

              PredictorOutput predictor_output;
              int matches = 0;
              MutableLineSequence predictions;
              for (const auto& search_path : search_paths) {
                VLOG(4) << "Considering search path: " << search_path;
                DescendDirectoryTreeOutput descend_results =
                    DescendDirectoryTree(search_path, path_input);
                if (descend_results.dir == nullptr) {
                  LOG(WARNING) << "Unable to descend: " << search_path;
                  continue;
                }
                predictor_output.longest_directory_match =
                    std::max(predictor_output.longest_directory_match,
                             ColumnNumberDelta(
                                 descend_results.valid_proper_prefix_length));
                CHECK_LE(descend_results.valid_prefix_length,
                         path_input.size());
                ScanDirectory(
                    descend_results.dir.get(), noise_regex,
                    path_input.substr(descend_results.valid_prefix_length,
                                      path_input.size()),
                    path_input.substr(0, descend_results.valid_prefix_length),
                    &matches, progress_channel, abort_value, predictions,
                    predictor_output);
                if (abort_value.has_value()) return PredictorOutput{};
              }
              SortedLineSequenceUniqueLines output_lines(
                  SortedLineSequence(std::move(predictions).snapshot()));
              predictor_output.contents = output_lines;
              return predictor_output;
            },
            std::ref(predictor_input.progress_channel),
            std::move(predictor_input.abort_value)));
      })
      .Transform([buffer = predictor_input.predictions.NewRoot()](
                     PredictorOutput predictor_output) {
        TRACK_OPERATION(FilePredictor_SetBufferContents);
        buffer.ptr()->InsertInPosition(
            predictor_output.contents.sorted_lines().lines(),
            buffer.ptr()->contents().range().end(), std::nullopt);
        return futures::Past(std::move(predictor_output));
      });
}

futures::Value<PredictorOutput> EmptyPredictor(PredictorInput) {
  return futures::Past(PredictorOutput{});
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
    // TODO(2023-12-02): Find a way to avoid the call to `ToString`.
    std::wstring input_str = input.input->ToString();
    for (auto it = contents->lower_bound(input_str); it != contents->end();
         ++it) {
      auto result =
          mismatch(input_str.begin(), input_str.end(), (*it).first.begin());
      if (result.first == input_str.end()) {
        input.predictions.AppendToLastLine(it->second);
        input.predictions.AppendRawLine(NonNull<std::shared_ptr<Line>>());
      } else {
        break;
      }
    }
    input.progress_channel.Push(ProgressInformation{
        .values = {
            {VersionPropertyKey(L"values"),
             std::to_wstring(input.predictions.lines_size().read() - 1)}}});
    return futures::Past(PredictorOutput(
        {.contents = SortedLineSequenceUniqueLines(
             SortedLineSequence(input.predictions.contents().snapshot()))}));
  };
}

namespace {
const bool buffer_tests_registration =
    tests::Register(L"PrecomputedPredictor", [] {
      const static Predictor test_predictor = PrecomputedPredictor(
          {L"foo", L"bar", L"bard", L"foo_bar", L"alejo"}, L'_');
      auto predict = [&](std::wstring input) {
        ChannelAll<ProgressInformation> channel([](ProgressInformation) {});
        NonNull<std::unique_ptr<EditorState>> editor = EditorForTests();
        gc::Root<OpenBuffer> buffer = NewBufferForTests(editor.value());
        test_predictor(PredictorInput{.editor = buffer.ptr()->editor(),
                                      .input = NewLazyString(input),
                                      .predictions = buffer.ptr().value(),
                                      .source_buffers = {},
                                      .progress_channel = channel});
        LOG(INFO) << "Contents: "
                  << buffer.ptr()->contents().snapshot().ToString();
        return buffer.ptr()->contents().snapshot().ToString();
      };
      auto test_predict = [&](std::wstring input,
                              std::function<void(PredictResults)> callback) {
        NonNull<std::unique_ptr<EditorState>> editor = EditorForTests();
        gc::Root<OpenBuffer> buffer = NewBufferForTests(editor.value());
        bool executed = false;
        Predict(PredictOptions{.editor_state = buffer.ptr()->editor(),
                               .predictor = test_predictor,
                               .text = input,
                               .source_buffers = {}})
            .Transform([&](std::optional<PredictResults> predict_results) {
              CHECK(!std::exchange(executed, true));
              CHECK(predict_results.has_value());
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
                [&] { CHECK(predict(L"bar") == L"bar\nfoo_bar\nbard\n"); }},
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
  // TODO(2023-10-09, Responsive): Move this to a background thread and use a
  // future instead.
  SortedLineSequenceUniqueLines contents(
      SortedLineSequence(dictionary_root.ptr()->contents().snapshot(),
                         [](const NonNull<std::shared_ptr<const Line>>& a,
                            const NonNull<std::shared_ptr<const Line>>& b) {
                           return a->ToString() < b->ToString();
                         }));

  return [contents](PredictorInput input) {
    auto input_line =
        MakeNonNullShared<const Line>(LineBuilder(input.input).Build());

    LineNumber line = contents.sorted_lines().upper_bound(input_line);

    // TODO(2023-12-02): Find a way to do this without `ToString`.
    const std::wstring input_str = input.input->ToString();

    // TODO: This has complexity N log N. We could instead extend BufferContents
    // to expose a wrapper around `Suffix`, allowing this to have complexity N
    // (just take the suffix once, and then walk it, with `ConstTree::Every`).
    while (line < contents.sorted_lines().lines().EndLine()) {
      auto line_contents = contents.sorted_lines().lines().at(line);
      auto line_str = line_contents->ToString();
      auto result =
          mismatch(input_str.begin(), input_str.end(), line_str.begin());
      if (result.first != input_str.end()) {
        break;
      }
      input.predictions.AppendRawLine(line_contents->contents());

      ++line;
    }

    // TODO(easy, 2023-10-08): Don't call SortedLineSequence here. Instead, add
    // methods to SortedLineSequence that allows us to extract a sub-range view,
    // and filter. There shouldn't be a need to re-sort.
    TRACK_OPERATION(DictionaryPredictor_Sorting);
    return futures::Past(PredictorOutput(
        {.contents = SortedLineSequenceUniqueLines(
             SortedLineSequence(input.predictions.contents().snapshot()))}));
  };
}

void RegisterLeaves(const OpenBuffer& buffer, const ParseTree& tree,
                    std::set<std::wstring>* words) {
  DCHECK(words != nullptr);
  if (tree.children().empty() &&
      tree.range().begin().line == tree.range().end().line) {
    CHECK_LE(tree.range().begin().column, tree.range().end().column);
    auto line = buffer.LineAt(tree.range().begin().line);
    auto word =
        line->Substring(tree.range().begin().column,
                        std::min(tree.range().end().column, line->EndColumn()) -
                            tree.range().begin().column)
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
  if (input.source_buffers.empty())
    return futures::Past(
        PredictorOutput({.contents = SortedLineSequenceUniqueLines(
                             SortedLineSequence(LineSequence()))}));
  std::set<std::wstring> words;
  for (const OpenBuffer& buffer : input.source_buffers | gc::view::Value) {
    RegisterLeaves(buffer, buffer.parse_tree().value(), &words);
    std::wistringstream keywords(
        buffer.Read(buffer_variables::language_keywords));
    words.insert(std::istream_iterator<std::wstring, wchar_t>(keywords),
                 std::istream_iterator<std::wstring, wchar_t>());
  }
  gc::Root<OpenBuffer> dictionary = OpenBuffer::New(
      {.editor = input.editor, .name = BufferName(L"Dictionary")});
  // TODO(2023-11-26, Ranges): Add a method to Buffer that takes the range
  // directly, to avoid the need to call MaterializeVector.
  dictionary.ptr()->AppendLines(container::MaterializeVector(
      words | std::views::transform([](std::wstring word) {
        return MakeNonNullShared<const Line>(Line(word));
      })));
  return DictionaryPredictor(gc::Root<const OpenBuffer>(std::move(dictionary)))(
      input);
}

Predictor ComposePredictors(Predictor a, Predictor b) {
  // TODO(easy, 2023-12-04): Use JoinValues instead. That would allow them to
  // execute concurrently.
  return [a, b](PredictorInput input) {
    gc::Root<OpenBuffer> a_predictions =
        OpenBuffer::New(OpenBuffer::Options{.editor = input.editor});
    gc::Root<OpenBuffer> b_predictions =
        OpenBuffer::New(OpenBuffer::Options{.editor = input.editor});
    return a(PredictorInput{.editor = input.editor,
                            .input = input.input,
                            .predictions = a_predictions.ptr().value(),
                            .source_buffers = input.source_buffers,
                            .progress_channel = input.progress_channel,
                            .abort_value = input.abort_value})
        .Transform([input, b, b_predictions](PredictorOutput a_output) {
          return b({.editor = input.editor,
                    .input = input.input,
                    .predictions = b_predictions.ptr().value(),
                    .source_buffers = input.source_buffers,
                    .progress_channel = input.progress_channel,
                    .abort_value = input.abort_value})
              .Transform([input, a_output](PredictorOutput b_output) {
                SortedLineSequenceUniqueLines merged_contents(
                    a_output.contents, b_output.contents);
                input.predictions.InsertInPosition(
                    merged_contents.sorted_lines().lines(), LineColumn(),
                    std::nullopt);
                TRACK_OPERATION(ComposePredictors_Sorting);
                return PredictorOutput(
                    {.contents = std::move(merged_contents)});
              });
        });
  };
}

}  // namespace afc::editor
