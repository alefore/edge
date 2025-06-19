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
#include "src/language/lazy_string/tokenize.h"
#include "src/language/overload.h"
#include "src/language/text/sorted_line_sequence.h"
#include "src/language/wstring.h"
#include "src/predictor.h"
#include "src/structure.h"
#include "src/tests/tests.h"
#include "src/vm/escape.h"

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
using afc::language::EmptyValue;
using afc::language::Error;
using afc::language::FromByteString;
using afc::language::IgnoreErrors;
using afc::language::IsError;
using afc::language::MakeNonNullShared;
using afc::language::MakeNonNullUnique;
using afc::language::NonNull;
using afc::language::OptionalFrom;
using afc::language::overload;
using afc::language::PossibleError;
using afc::language::Success;
using afc::language::ValueOrDie;
using afc::language::ValueOrError;
using afc::language::lazy_string::ColumnNumber;
using afc::language::lazy_string::ColumnNumberDelta;
using afc::language::lazy_string::FindFirstOf;
using afc::language::lazy_string::LazyString;
using afc::language::lazy_string::NonEmptySingleLine;
using afc::language::lazy_string::SingleLine;
using afc::language::lazy_string::Token;
using afc::language::lazy_string::TokenizeBySpaces;
using afc::language::text::Line;
using afc::language::text::LineBuilder;
using afc::language::text::LineColumn;
using afc::language::text::LineNumber;
using afc::language::text::LineNumberDelta;
using afc::language::text::LineSequence;
using afc::language::text::LineSequenceIterator;
using afc::language::text::MutableLineSequence;
using afc::language::text::SortedLineSequence;
using afc::language::text::SortedLineSequenceUniqueLines;
using afc::language::view::SkipErrors;
using afc::vm::EscapedString;

namespace afc::editor {
namespace {
ValueOrError<PredictResults> BuildResults(
    EditorState& editor, PredictorOutput predictor_output,
    DeleteNotification::Value& abort_value) {
  TRACK_OPERATION(Predictor_BuildResults);
  if (abort_value.has_value()) return Error{LazyString{L"Aborted"}};

  std::optional<std::wstring> common_prefix;
  predictor_output.contents.read().lines().EveryLine(
      [&common_prefix, &abort_value](LineNumber, const Line& line) {
        if (abort_value.has_value()) return false;
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
        // TODO(easy, 2024-09-10): Get rid of std::wstring. Use SingleLine and
        // StartsWith and LowerCase.
        std::wstring current =
            line.Substring(ColumnNumber(0), current_size).read().ToString();

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
  if (abort_value.has_value()) return Error{LazyString{L"Aborted"}};
  CHECK(predictor_output.contents.read().lines().EndLine() == LineNumber(0) ||
        !predictor_output.contents.read().lines().at(LineNumber()).empty());
  gc::Root<OpenBuffer> predictions_buffer = OpenBuffer::New(
      OpenBuffer::Options{.editor = editor, .name = PredictionsBufferName{}});
  predictions_buffer.ptr()->Set(buffer_variables::show_in_buffers_list, false);
  predictions_buffer.ptr()->Set(buffer_variables::allow_dirty_delete, true);
  predictions_buffer.ptr()->Set(buffer_variables::paste_mode, true);
  TRACK_OPERATION(Predictor_BuildResult_InsertInPosition);
  predictions_buffer.ptr()->InsertInPosition(
      predictor_output.contents.read().lines(), LineColumn(), std::nullopt);
  return PredictResults{
      .common_prefix = common_prefix,
      .predictions_buffer = predictions_buffer,
      .matches =
          predictor_output.contents.read().lines().EndLine() == LineNumber(0) &&
                  predictor_output.contents.read()
                      .lines()
                      .at(LineNumber())
                      .empty()
              ? 0
              : predictions_buffer.ptr()->lines_size().read(),
      .predictor_output = predictor_output};
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

futures::Value<std::optional<PredictResults>> Predict(
    const Predictor& predictor, PredictorInput options) {
  futures::Future<std::optional<PredictResults>> output;
  CHECK(!options.abort_value.has_value());

  return predictor(PredictorInput{.editor = options.editor,
                                  .input = options.input,
                                  .input_column = options.input_column,
                                  .source_buffers = options.source_buffers,
                                  .progress_channel = options.progress_channel,
                                  .abort_value = options.abort_value})
      .Transform([&editor = options.editor, abort_value = options.abort_value,
                  progress_channel = options.progress_channel](
                     PredictorOutput predictor_output) mutable
                     -> ValueOrError<PredictResults> {
        DECLARE_OR_RETURN(auto results,
                          BuildResults(editor, predictor_output, abort_value));
        if (abort_value.has_value()) return Error{LazyString{L"Aborted"}};
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
  ValueOrError<NonNull<std::unique_ptr<DIR, std::function<void(DIR*)>>>> dir;
  // The length of the longest prefix of path that corresponds to a valid
  // directory.
  ColumnNumberDelta valid_prefix_length;
  ColumnNumberDelta valid_proper_prefix_length;
};

DescendDirectoryTreeOutput DescendDirectoryTree(Path search_path,
                                                LazyString path) {
  VLOG(6) << "Starting search at: " << search_path;
  DescendDirectoryTreeOutput output;
  output.dir = OpenDir(search_path);
  if (IsError(output.dir)) {
    VLOG(5) << "Unable to open search_path: " << search_path;
    return output;
  }

  // We don't use DirectorySplit in order to handle adjacent slashes.
  while (output.valid_prefix_length < path.size()) {
    output.valid_proper_prefix_length = output.valid_prefix_length;
    VLOG(6) << "Iterating at: "
            << path.Substring(ColumnNumber{}, output.valid_prefix_length);
    std::optional<ColumnNumber> next_candidate = FindFirstOf(
        path.Substring(ColumnNumber{} + output.valid_prefix_length), {L'/'});
    if (next_candidate != std::nullopt)
      *next_candidate += output.valid_prefix_length;

    if (next_candidate == std::nullopt) {
      next_candidate = ColumnNumber{} + path.size();
    } else if (next_candidate->ToDelta() == output.valid_prefix_length) {
      ++output.valid_prefix_length;
      continue;
    } else {
      ++*next_candidate;
    }
    auto path_next_candidate =
        Path::New(path.Substring(ColumnNumber{}, next_candidate->ToDelta()));
    if (IsError(path_next_candidate)) continue;
    Path test_path =
        Path::Join(search_path, ValueOrDie(std::move(path_next_candidate)));
    VLOG(8) << "Considering: " << test_path;
    auto subdir = OpenDir(test_path);
    if (IsError(subdir)) return output;
    output.dir = std::move(subdir);
    output.valid_prefix_length = next_candidate->ToDelta();
  }
  return output;
}

// Reads the entire contents of `dir`, looking for files that match `pattern`.
// For any files that do, prepends `prefix` and appends them to `buffer`.
void ScanDirectory(DIR& dir, const std::wregex& noise_regex,
                   std::wstring pattern, std::wstring prefix, int* matches,
                   ProgressChannel& progress_channel,
                   DeleteNotification::Value& abort_value,
                   MutableLineSequence& output_lines,
                   PredictorOutput& predictor_output) {
  TRACK_OPERATION(FilePredictor_ScanDirectory);

  VLOG(5) << "Scanning directory \"" << prefix << "\" looking for: " << pattern;
  // The length of the longest prefix of `pattern` that matches an entry.
  size_t longest_pattern_match = 0;
  struct dirent* entry;

  while ((entry = readdir(&dir)) != nullptr) {
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
    if (std::regex_match(full_path, noise_regex)) continue;
    output_lines.push_back(
        LineBuilder{EscapedString::FromString(LazyString{std::move(full_path)})
                        .EscapedRepresentation()}
            .Build(),
        MutableLineSequence::ObserverBehavior::kHide);
    ++*matches;
    if (*matches % 100 == 0)
      progress_channel.Push(ProgressInformation{
          .values = {
              {VersionPropertyKey{NON_EMPTY_SINGLE_LINE_CONSTANT(L"files")},
               NonEmptySingleLine(*matches).read()}}});
  }

  progress_channel.Push(ProgressInformation{
      .values = {{VersionPropertyKey{NON_EMPTY_SINGLE_LINE_CONSTANT(L"files")},
                  NonEmptySingleLine(*matches).read()}}});

  predictor_output.longest_prefix =
      std::max(predictor_output.longest_prefix,
               ColumnNumberDelta(prefix.size() + longest_pattern_match));
  if (pattern.empty()) {
    predictor_output.found_exact_match = true;
  }
}

futures::Value<PredictorOutput> FilePredictor(PredictorInput predictor_input) {
  LOG(INFO) << "Generating predictions for: " << predictor_input.input;
  return GetSearchPaths(predictor_input.editor)
      .Transform([predictor_input](std::vector<Path> search_paths) {
        // We can't use a Path type because this comes from the prompt and ...
        // may not actually be a valid path.
        LazyString path_input = std::visit(
            overload{[&](Error) { return predictor_input.input.read(); },
                     [&](Path path) {
                       return predictor_input.editor.expand_path(path).read();
                     }},
            Path::New(ToLazyString(predictor_input.input)));

        // TODO: Don't use sources_buffers[0], ignoring the other buffers.
        std::wregex noise_regex =
            predictor_input.source_buffers.empty()
                ? std::wregex()
                : std::wregex(predictor_input.source_buffers[0]
                                  .ptr()
                                  ->Read(buffer_variables::directory_noise)
                                  .ToString());
        return predictor_input.editor.thread_pool().Run(std::bind_front(
            [path_input, search_paths, noise_regex](
                NonNull<std::shared_ptr<ProgressChannel>> progress_channel,
                DeleteNotification::Value abort_value) mutable {
              if (!path_input.empty() &&
                  path_input.get(ColumnNumber{}) == L'/') {
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
                if (IsError(descend_results.dir)) {
                  LOG(WARNING) << "Unable to descend: " << search_path;
                  continue;
                }
                predictor_output.longest_directory_match =
                    std::max(predictor_output.longest_directory_match,
                             ColumnNumberDelta(
                                 descend_results.valid_proper_prefix_length));
                CHECK_LE(descend_results.valid_prefix_length,
                         path_input.size());
                // TODO(trivial, 2024-08-26): Get rid of ToString.
                ScanDirectory(
                    ValueOrDie(std::move(descend_results.dir)).value(),
                    noise_regex,
                    path_input
                        .Substring(ColumnNumber{} +
                                   descend_results.valid_prefix_length)
                        .ToString(),
                    path_input
                        .Substring(ColumnNumber{},
                                   descend_results.valid_prefix_length)
                        .ToString(),
                    &matches, progress_channel.value(), abort_value,
                    predictions, predictor_output);
                if (abort_value.has_value()) return PredictorOutput{};
              }
              predictions.MaybeEraseEmptyFirstLine();
              SortedLineSequenceUniqueLines output_lines(
                  SortedLineSequence(std::move(predictions).snapshot()));
              predictor_output.contents = output_lines;
              return predictor_output;
            },
            predictor_input.progress_channel,
            std::move(predictor_input.abort_value)));
      });
}

futures::Value<PredictorOutput> EmptyPredictor(PredictorInput) {
  return futures::Past(PredictorOutput{});
}

namespace {
std::vector<NonEmptySingleLine> RegisterVariations(SingleLine prediction,
                                                   wchar_t separator) {
  DVLOG(5) << "Generating predictions for: " << prediction;
  return container::MaterializeVector(
      std::invoke([&] {
        std::vector<ColumnNumber> starting_position;
        std::optional<ColumnNumber> next =
            FindFirstNotOf(prediction, {separator});
        while (next.has_value()) {
          starting_position.push_back(next.value());
          next = FindFirstOf(prediction, {separator}, next.value());
          if (next.has_value())
            next = FindFirstNotOf(prediction, {separator}, next.value());
        }
        return starting_position;
      }) |
      std::views::transform([&prediction](ColumnNumber start) {
        return NonEmptySingleLine::New(prediction.Substring(start));
      }) |
      language::view::SkipErrors);
}

const bool register_variations_tests_registration = tests::Register(
    L"RegisterVariations",
    {{.name = L"SimpleCall", .callback = [] {
        CHECK(RegisterVariations(SingleLine{LazyString{L"    foo    bar  "}},
                                 L' ') ==
              std::vector<NonEmptySingleLine>(
                  {NonEmptySingleLine{SingleLine{LazyString{L"foo    bar  "}}},
                   NonEmptySingleLine{SingleLine{LazyString{L"bar  "}}}}));
      }}});
}  // namespace

Predictor PrecomputedPredictor(
    const std::vector<NonEmptySingleLine>& predictions, wchar_t separator) {
  const NonNull<
      std::shared_ptr<std::multimap<NonEmptySingleLine, NonEmptySingleLine>>>
      contents;
  for (const NonEmptySingleLine& prediction : predictions)
    std::ranges::copy(
        RegisterVariations(prediction.read(), separator) |
            std::views::transform([&prediction](const NonEmptySingleLine& key) {
              return std::make_pair(key, prediction);
            }),
        std::inserter(contents.value(), contents->end()));
  return [contents](PredictorInput input) {
    ValueOrError<NonEmptySingleLine> input_value =
        NonEmptySingleLine::New(input.input);
    if (IsError(input_value)) return futures::Past(PredictorOutput{});
    MutableLineSequence output_contents;
    for (auto it = contents->lower_bound(ValueOrDie(std::move(input_value)));
         it != contents->end(); ++it) {
      if (StartsWith((*it).first, input.input)) {
        output_contents.push_back(LineBuilder{it->second.read()}.Build());
      } else {
        break;
      }
    }
    output_contents.MaybeEraseEmptyFirstLine();

    input.progress_channel->Push(ProgressInformation{
        .values = {
            {VersionPropertyKey{NON_EMPTY_SINGLE_LINE_CONSTANT(L"values")},
             NonEmptySingleLine(output_contents.size().read() - 1).read()}}});
    return futures::Past(
        PredictorOutput({.contents = SortedLineSequenceUniqueLines(
                             SortedLineSequence(output_contents.snapshot()))}));
  };
}

namespace {
const bool precomputed_predictor_tests_registration =
    tests::Register(L"PrecomputedPredictor", [] {
      const static Predictor test_predictor = PrecomputedPredictor(
          {NonEmptySingleLine{SingleLine{LazyString{L"foo"}}},
           NonEmptySingleLine{SingleLine{LazyString{L"bar"}}},
           NonEmptySingleLine{SingleLine{LazyString{L"bard"}}},
           NonEmptySingleLine{SingleLine{LazyString{L"foo_bar"}}},
           NonEmptySingleLine{SingleLine{LazyString{L"alejo"}}}},
          L'_');
      auto predict = [&](std::wstring input) {
        NonNull<std::unique_ptr<EditorState>> editor =
            EditorForTests(std::nullopt);
        PredictorOutput output =
            test_predictor(
                PredictorInput{
                    .editor = editor.value(),
                    .input = SingleLine{LazyString{input}},
                    .input_column = ColumnNumber(input.size()),
                    .source_buffers = {},
                    .progress_channel =
                        MakeNonNullUnique<ChannelAll<ProgressInformation>>(
                            [](ProgressInformation) {})})
                .Get()
                .value();
        LineSequence lines = output.contents.read().lines();
        LOG(INFO) << "Contents: " << lines.ToString();
        return lines.ToString();
      };
      auto test_predict = [&](std::wstring input,
                              std::function<void(PredictResults)> callback) {
        NonNull<std::unique_ptr<EditorState>> editor =
            EditorForTests(std::nullopt);
        bool executed = false;
        Predict(test_predictor,
                PredictorInput{.editor = editor.value(),
                               .input = SingleLine{LazyString{input}},
                               .input_column = ColumnNumber(input.size()),
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
            .callback = [&] { CHECK(predict(L"ale") == L"alejo"); }},
           {.name = L"CallTokenPrediction",
            .callback =
                [&] { CHECK(predict(L"bar") == L"bar\nbard\nfoo_bar"); }},
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
      SortedLineSequence(dictionary_root.ptr()->contents().snapshot()));

  return [contents](PredictorInput input) {
    Line input_line{input.input};
    // TODO: This has complexity N log N. We could instead extend BufferContents
    // to expose a wrapper around `Suffix`, allowing this to have complexity N
    // (just take the suffix once, and then walk it, with `ConstTree::Every`).
    const LineSequenceIterator begin = contents.read().upper_bound(input_line);
    LineSequenceIterator end = begin;
    while (end != contents.read().lines().end() &&
           StartsWith((*end).contents(), input.input))
      ++end;

    // TODO(easy, 2023-10-08): Don't call SortedLineSequence here. Instead, add
    // methods to SortedLineSequence that allows us to extract a sub-range view,
    // and filter. There shouldn't be a need to re-sort.
    TRACK_OPERATION(DictionaryPredictor_Sorting);
    return futures::Past(
        PredictorOutput({.contents = SortedLineSequenceUniqueLines{
                             SortedLineSequence{LineSequence{begin, end}}}}));
  };
}

void RegisterLeaves(const OpenBuffer& buffer, const ParseTree& tree,
                    std::set<NonEmptySingleLine>* words) {
  DCHECK(words != nullptr);
  if (tree.children().empty() &&
      tree.range().begin().line == tree.range().end().line) {
    CHECK_LE(tree.range().begin().column, tree.range().end().column);
    std::optional<Line> line = buffer.LineAt(tree.range().begin().line);
    CHECK(line.has_value());
    std::visit(overload{[&words](NonEmptySingleLine word) {
                          DVLOG(5) << "Found leave: " << word;
                          words->insert(word);
                        },
                        IgnoreErrors{}},
               NonEmptySingleLine::New(line->Substring(
                   tree.range().begin().column,
                   std::min(tree.range().end().column, line->EndColumn()) -
                       tree.range().begin().column)));
  }
  for (auto& child : tree.children()) RegisterLeaves(buffer, child, words);
}

futures::Value<PredictorOutput> SyntaxBasedPredictor(PredictorInput input) {
  if (input.source_buffers.empty())
    return futures::Past(
        PredictorOutput({.contents = SortedLineSequenceUniqueLines(
                             SortedLineSequence(LineSequence()))}));
  std::set<NonEmptySingleLine> words;
  for (const OpenBuffer& buffer : input.source_buffers | gc::view::Value) {
    RegisterLeaves(buffer, buffer.parse_tree().value(), &words);
    std::ranges::copy(
        TokenizeBySpaces(LineSequence::BreakLines(
                             buffer.Read(buffer_variables::language_keywords))
                             .FoldLines()) |
            std::views::transform(&Token::value),
        std::inserter(words, words.end()));
  }
  gc::Root<OpenBuffer> dictionary = OpenBuffer::New(
      {.editor = input.editor, .name = BufferName{LazyString{L"Dictionary"}}});
  // TODO(2023-11-26, Ranges): Add a method to Buffer that takes the range
  // directly, to avoid the need to call MaterializeVector.
  dictionary.ptr()->AppendLines(container::MaterializeVector(
      words | std::views::transform(
                  [](NonEmptySingleLine word) { return Line{word.read()}; })));
  return DictionaryPredictor(gc::Root<const OpenBuffer>(std::move(dictionary)))(
      input);
}

Predictor ComposePredictors(Predictor a, Predictor b) {
  return [a, b](PredictorInput input) {
    return JoinValues(
               a(PredictorInput{.editor = input.editor,
                                .input = input.input,
                                .input_column = input.input_column,
                                .source_buffers = input.source_buffers,
                                .progress_channel = input.progress_channel,
                                .abort_value = input.abort_value}),
               b(PredictorInput{.editor = input.editor,
                                .input = input.input,
                                .input_column = input.input_column,
                                .source_buffers = input.source_buffers,
                                .progress_channel = input.progress_channel,
                                .abort_value = input.abort_value}))
        .Transform(
            [input](std::tuple<PredictorOutput, PredictorOutput> outputs) {
              TRACK_OPERATION(ComposePredictors_Sorting);
              return PredictorOutput{.contents = SortedLineSequenceUniqueLines(
                                         std::get<0>(outputs).contents,
                                         std::get<1>(outputs).contents)};
            });
  };
}

}  // namespace afc::editor
