#include "src/search_handler.h"

#include <iostream>
#include <regex>
#include <set>

#include "src/buffer_variables.h"
#include "src/infrastructure/audio.h"
#include "src/language/container.h"
#include "src/language/gc_view.h"
#include "src/language/lazy_string/append.h"
#include "src/language/lazy_string/char_buffer.h"
#include "src/language/lazy_string/functional.h"
#include "src/language/wstring.h"
#include "src/tests/tests.h"

namespace gc = afc::language::gc;
namespace audio = afc::infrastructure::audio;

using afc::concurrent::ChannelAll;
using afc::concurrent::VersionPropertyKey;
using afc::concurrent::WorkQueue;
using afc::language::EmptyValue;
using afc::language::EraseIf;
using afc::language::Error;
using afc::language::FromByteString;
using afc::language::MakeNonNullShared;
using afc::language::NonNull;
using afc::language::PossibleError;
using afc::language::Success;
using afc::language::ValueOrError;
using afc::language::lazy_string::Append;
using afc::language::lazy_string::ColumnNumber;
using afc::language::lazy_string::LazyString;
using afc::language::lazy_string::NewLazyString;
using afc::language::text::Line;
using afc::language::text::LineBuilder;
using afc::language::text::LineColumn;
using afc::language::text::LineNumber;
using afc::language::text::LineSequence;
using afc::language::text::MutableLineSequence;
using afc::language::text::Range;
using afc::language::text::SortedLineSequence;
using afc::language::text::SortedLineSequenceUniqueLines;

namespace afc::editor {
namespace {

typedef std::wregex RegexPattern;

// Returns all columns where the current line matches the pattern.
std::vector<ColumnNumber> GetMatches(const std::wstring& line,
                                     const RegexPattern& pattern) {
  size_t start = 0;
  std::vector<ColumnNumber> output;
  while (start < line.size()) {
    size_t match = std::wstring::npos;
    std::wstring line_substr = line.substr(start);

    std::wsmatch pattern_match;
    std::regex_search(line_substr, pattern_match, pattern);
    if (!pattern_match.empty()) {
      match = pattern_match.position();
    }
    if (match == std::wstring::npos) {
      return output;
    }
    output.push_back(ColumnNumber(start + match));
    start += match + 1;
  }
  return output;
}

auto GetRegexTraits(bool case_sensitive) {
  auto traits = std::regex_constants::extended;
  if (!case_sensitive) {
    traits |= std::regex_constants::icase;
  }
  return traits;
}

ValueOrError<std::vector<LineColumn>> PerformSearch(
    const SearchOptions& options, const LineSequence& contents,
    size_t previously_found_matches,
    std::function<bool(const LineColumn&)> predicate) {
  std::wregex pattern;
  try {
    pattern = std::wregex(options.search_query->ToString(),
                          GetRegexTraits(options.case_sensitive));
  } catch (std::regex_error& e) {
    Error error(L"Regex failure: " + FromByteString(e.what()));
    options.progress_channel->Push(
        {.values = {{VersionPropertyKey(L"!"), error.read()}}});
    return error;
  }

  std::vector<LineColumn> positions;
  contents.EveryLine([&](LineNumber position,
                         const NonNull<std::shared_ptr<const Line>>& line) {
    auto matches = GetMatches(line->ToString(), pattern);
    size_t initial_size = positions.size();
    std::ranges::copy(
        matches | std::views::transform([position](ColumnNumber column) {
          return LineColumn(position, column);
        }) | std::views::filter(predicate),
        std::back_inserter(positions));
    if (positions.size() > initial_size)
      options.progress_channel->Push(ProgressInformation{
          .counters = {{VersionPropertyKey(L"matches"),
                        previously_found_matches + positions.size()}}});
    if (!options.abort_value.has_value() &&
        (!options.required_positions.has_value() ||
         options.required_positions.value() > positions.size()))
      return true;
    options.progress_channel->Push(
        ProgressInformation{.values = {{VersionPropertyKey(L"partial"), L""}}});
    return false;
  });
  VLOG(5) << "Perform search found matches: " << positions.size();
  return positions;
}

}  // namespace

std::ostream& operator<<(std::ostream& os, const SearchResultsSummary& a) {
  os << "[search results summary: matches:" << a.matches << ", completion:";
  switch (a.search_completion) {
    case SearchResultsSummary::SearchCompletion::kInterrupted:
      os << "interrupted";
      break;
    case SearchResultsSummary::SearchCompletion::kFull:
      os << "full";
      break;
  }
  os << "]";
  return os;
}

bool operator==(const SearchResultsSummary& a, const SearchResultsSummary& b) {
  return a.matches == b.matches && a.search_completion == b.search_completion;
}

std::wstring RegexEscape(NonNull<std::shared_ptr<LazyString>> str) {
  std::wstring results;
  static std::wstring literal_characters = L" ()<>{}+_-;\"':,?#%";
  ForEachColumn(str.value(), [&](ColumnNumber, wchar_t c) {
    if (!iswalnum(c) && literal_characters.find(c) == std::wstring::npos) {
      results.push_back('\\');
    }
    results.push_back(c);
  });
  return results;
}

PossibleError SearchInBuffer(PredictorInput& input, OpenBuffer& buffer,
                             size_t required_positions,
                             std::set<std::wstring>& matches) {
  ASSIGN_OR_RETURN(std::vector<LineColumn> positions,
                   buffer.status().LogErrors(SearchHandler(
                       input.editor.modifiers().direction,
                       SearchOptions{.starting_position = buffer.position(),
                                     .search_query = input.input},
                       buffer.contents().snapshot())));

  // Get the first kMatchesLimit matches:
  if (!positions.empty()) buffer.set_position(positions[0]);
  std::ranges::copy(
      positions | std::views::take(required_positions) |
          std::views::transform([&](LineColumn& position) -> std::wstring {
            CHECK_LT(position.line, buffer.EndLine());
            auto line = buffer.LineAt(position.line);
            CHECK_LT(position.column, line->EndColumn());
            return RegexEscape(line->Substring(position.column));
          }),
      std::inserter(matches, matches.end()));
  return Success();
}

futures::Value<PredictorOutput> SearchHandlerPredictor(PredictorInput input) {
  // TODO(2023-10-08, easy): This whole function could probably be optimized. We
  // could probably add matches directly to the sorted contents; or, at least,
  // build the sorted contents from matches directly.
  std::set<std::wstring> matches;
  static constexpr int kMatchesLimit = 100;
  for (OpenBuffer& search_buffer : input.source_buffers | gc::view::Value)
    SearchInBuffer(input, search_buffer, kMatchesLimit, matches);
  MutableLineSequence output_contents;
  std::ranges::copy(
      std::move(matches) | std::views::transform([](std::wstring match) {
        return MakeNonNullShared<Line>(Line(match));
      }),
      std::back_inserter(output_contents));
  output_contents.MaybeEraseEmptyFirstLine();
  TRACK_OPERATION(SearchHandlerPredictor_sort);
  return futures::Past(
      PredictorOutput{.contents = SortedLineSequenceUniqueLines(
                          SortedLineSequence(output_contents.snapshot()))});
}

ValueOrError<std::vector<LineColumn>> SearchHandler(
    Direction direction, const SearchOptions& options,
    const LineSequence& contents) {
  if (options.search_query->size().IsZero()) {
    return {};
  }

  const LineColumn starting_position =
      std::min(options.starting_position, contents.range().end());

  Range range_before = Range(LineColumn(), starting_position);
  Range range_after = Range(std::min(contents.range().end(), starting_position),
                            contents.range().end());
  if (options.limit_position.has_value()) {
    if (*options.limit_position < starting_position)
      range_before.set_begin(
          std::min(*options.limit_position, range_before.end()));
    else
      range_after.set_end(
          std::max(range_after.begin(), *options.limit_position));
  }

  // We extend `range_before` to cover the entire line, in case the starting
  // position is in the middle of a match. We account for that afterwards.
  range_before.set_end_column(std::numeric_limits<ColumnNumber>::max());

  // We should skip the current position. Start searching strictly after the
  // current position. But avoid adjusting the range if it's already empty.
  if (!range_after.IsEmpty())
    range_after.set_begin(LineColumn(range_after.begin().line,
                                     range_after.begin().column.next()));

  auto Search = [&options, &contents](
                    const Range& range, size_t previously_found_matches,
                    std::function<bool(const LineColumn&)> predicate)
      -> ValueOrError<std::vector<LineColumn>> {
    DECLARE_OR_RETURN(std::vector<LineColumn> results,
                      PerformSearch(options, contents.ViewRange(range),
                                    previously_found_matches, predicate));
    if (!range.begin().column.IsZero())
      for (LineColumn& result : results) {
        result.line += range.begin().line.ToDelta();
        if (result.line == range.begin().line)
          result.column += range.begin().column.ToDelta();
      }
    return results;
  };

  DECLARE_OR_RETURN(
      std::vector<LineColumn> output,
      Search(range_after, 0, [](const LineColumn&) { return true; }));

  DECLARE_OR_RETURN(std::vector<LineColumn> results_before,
                    Search(range_before, output.size(),
                           // Account for the fact that we extended
                           // `range_before` past the starting position
                           [starting_position](const LineColumn& candidate) {
                             return candidate <= starting_position;
                           }));

  output.insert(output.end(), results_before.begin(), results_before.end());
  switch (direction) {
    case Direction::kForwards:
      break;
    case Direction::kBackwards:
      std::reverse(output.begin(), output.end());
      break;
  }
  return output;
}

namespace {
bool tests_search_handler_register = tests::Register(L"SearchHandler", [] {
  LineSequence contents =
      LineSequence::ForTests({L"Alejandro", L"Forero", L"Cuervo"});
  return std::vector<tests::Test>(
      {{.name = L"NoMatch",
        .callback =
            [=] {
              CHECK(ValueOrDie(
                        SearchHandler(
                            Direction::kForwards,
                            SearchOptions{
                                .starting_position = LineColumn(
                                    contents.range().end().line,
                                    std::numeric_limits<ColumnNumber>::max()),
                                .search_query = NewLazyString(L"xxxx"),
                                .required_positions = std::nullopt,
                                .case_sensitive = false},
                            contents))
                        .empty());
            }},
       {.name = L"WithPositionAtEndInfiniteColumn",
        .callback =
            [=] {
              CHECK(ValueOrDie(SearchHandler(
                        Direction::kForwards,
                        SearchOptions{
                            .starting_position = LineColumn(
                                contents.range().end().line,
                                std::numeric_limits<ColumnNumber>::max()),
                            .search_query = NewLazyString(L"rero"),
                            .required_positions = std::nullopt,
                            .case_sensitive = false},
                        contents)) ==
                    std::vector<LineColumn>(
                        {LineColumn(LineNumber(1), ColumnNumber(2))}));
            }},
       {.name = L"WithPositionAtEnd",
        .callback =
            [=] {
              CHECK(
                  ValueOrDie(SearchHandler(
                      Direction::kForwards,
                      SearchOptions{.starting_position = contents.range().end(),
                                    .search_query = NewLazyString(L"rero"),
                                    .required_positions = std::nullopt,
                                    .case_sensitive = false},
                      contents)) ==
                  std::vector<LineColumn>(
                      {LineColumn(LineNumber(1), ColumnNumber(2))}));
            }},
       {.name = L"SomeMatchesBackwards",
        .callback =
            [=] {
              CHECK(ValueOrDie(SearchHandler(
                        Direction::kBackwards,
                        SearchOptions{.starting_position = LineColumn(
                                          LineNumber(1), ColumnNumber(3)),
                                      .search_query = NewLazyString(L"r"),
                                      .required_positions = std::nullopt,
                                      .case_sensitive = false},
                        contents)) ==
                    std::vector<LineColumn>(
                        {LineColumn(LineNumber(1), ColumnNumber(2)),
                         LineColumn(LineNumber(0), ColumnNumber(7)),
                         LineColumn(LineNumber(2), ColumnNumber(3)),
                         LineColumn(LineNumber(1), ColumnNumber(4))}));
            }},
       {.name = L"SomeMatches",
        .callback =
            [=] {
              CHECK(ValueOrDie(SearchHandler(
                        Direction::kForwards,
                        SearchOptions{.starting_position = LineColumn(
                                          LineNumber(0), ColumnNumber(7)),
                                      .search_query = NewLazyString(L"ro"),
                                      .required_positions = std::nullopt,
                                      .case_sensitive = false},
                        contents)) ==
                    std::vector<LineColumn>({
                        LineColumn(LineNumber(1), ColumnNumber(4)),
                        LineColumn(LineNumber(0), ColumnNumber(7)),
                    }));
            }},
       {.name = L"ReachMatchLimit", .callback = [=] {
          CHECK(
              ValueOrDie(SearchHandler(
                  Direction::kForwards,
                  SearchOptions{.starting_position =
                                    LineColumn(LineNumber(1), ColumnNumber(3)),
                                .search_query = NewLazyString(L"."),
                                .required_positions = 1,
                                .case_sensitive = false},
                  contents)) == std::vector<LineColumn>({
                                    LineColumn(LineNumber(1), ColumnNumber(4)),
                                    LineColumn(LineNumber(1), ColumnNumber(5)),
                                    LineColumn(LineNumber(0), ColumnNumber(0)),
                                    LineColumn(LineNumber(0), ColumnNumber(1)),
                                    LineColumn(LineNumber(0), ColumnNumber(2)),
                                    LineColumn(LineNumber(0), ColumnNumber(3)),
                                    LineColumn(LineNumber(0), ColumnNumber(4)),
                                    LineColumn(LineNumber(0), ColumnNumber(5)),
                                    LineColumn(LineNumber(0), ColumnNumber(6)),
                                    LineColumn(LineNumber(0), ColumnNumber(7)),
                                    LineColumn(LineNumber(0), ColumnNumber(8)),
                                }));
        }}});
}());
}

ValueOrError<LineColumn> GetNextMatch(Direction direction,
                                      const SearchOptions& options,
                                      const LineSequence& contents) {
  if (std::optional<std::vector<LineColumn>> results =
          OptionalFrom(SearchHandler(direction, options, contents));
      results.has_value() && !results->empty())
    return results->at(0);

  // TODO(easy, 2023-09-09): Get rid of ToString.
  return Error(
      Append(NewLazyString(L"No matches: "), options.search_query)->ToString());
}

void HandleSearchResults(
    const ValueOrError<std::vector<LineColumn>>& results_or_error,
    OpenBuffer& buffer) {
  const std::vector<LineColumn>* results = std::get_if<0>(&results_or_error);
  if (results == nullptr) {
    buffer.status().ConsumeErrors(results_or_error, {});
    return;
  }

  if (results->empty()) {
    buffer.status().SetInformationText(
        MakeNonNullShared<Line>(L"🔍 No results."));
    audio::BeepFrequencies(buffer.editor().audio_player(), 0.1,
                           {audio::Frequency(659.25), audio::Frequency(440.0),
                            audio::Frequency(440.0)});

    if (buffer.Read(buffer_variables::search_filter_buffer)) {
      buffer.editor().CloseBuffer(buffer);
    }
    return;
  }

  buffer.set_active_cursors(*results);
  buffer.ResetMode();

  size_t size = results->size();
  if (size == 1) {
    buffer.status().SetInformationText(
        MakeNonNullShared<Line>(L"🔍 1 result."));
  } else {
    // TODO(easy, 2023-09-08): Convert `results_prefix` to use Padding?
    std::wstring results_prefix(1 + static_cast<size_t>(log2(size)), L'🔍');
    buffer.status().SetInformationText(MakeNonNullShared<Line>(
        LineBuilder(Append(NewLazyString(results_prefix),
                           NewLazyString(L" Results: "),
                           NewLazyString(std::to_wstring(size))))
            .Build()));
  }
  std::vector<audio::Frequency> frequencies = {
      audio::Frequency(440.0), audio::Frequency(440.0),
      audio::Frequency(493.88), audio::Frequency(523.25),
      audio::Frequency(587.33)};
  frequencies.resize(std::min(frequencies.size(), size + 1),
                     audio::Frequency(0.0));
  audio::BeepFrequencies(buffer.editor().audio_player(), 0.1, frequencies);
  buffer.Set(buffer_variables::multiple_cursors, false);
}

}  // namespace afc::editor
