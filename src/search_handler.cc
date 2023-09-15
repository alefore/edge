#include "src/search_handler.h"

#include <iostream>
#include <regex>
#include <set>

#include "src/buffer_variables.h"
#include "src/infrastructure/audio.h"
#include "src/language/lazy_string/append.h"
#include "src/language/lazy_string/char_buffer.h"
#include "src/language/lazy_string/functional.h"
#include "src/language/wstring.h"

namespace afc::editor {
namespace {

using std::vector;
using std::wstring;

using concurrent::VersionPropertyKey;
using concurrent::WorkQueueChannelConsumeMode;
using language::EmptyValue;
using language::Error;
using language::FromByteString;
using language::MakeNonNullShared;
using language::NonNull;
using language::PossibleError;
using language::Success;
using language::ValueOrError;
using language::lazy_string::Append;
using language::lazy_string::ColumnNumber;
using language::lazy_string::LazyString;
using language::lazy_string::NewLazyString;
using language::text::Line;
using language::text::LineBuilder;
using language::text::LineColumn;
using language::text::LineNumber;
using language::text::LineSequence;
using language::text::Range;

namespace gc = language::gc;
namespace audio = afc::infrastructure::audio;

static constexpr int kMatchesLimit = 100;

typedef std::wregex RegexPattern;

// Returns all columns where the current line matches the pattern.
vector<ColumnNumber> GetMatches(const wstring& line,
                                const RegexPattern& pattern) {
  size_t start = 0;
  vector<ColumnNumber> output;
  while (start < line.size()) {
    size_t match = wstring::npos;
    wstring line_substr = line.substr(start);

    std::wsmatch pattern_match;
    std::regex_search(line_substr, pattern_match, pattern);
    if (!pattern_match.empty()) {
      match = pattern_match.position();
    }
    if (match == wstring::npos) {
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
    ProgressChannel* progress_channel) {
  std::vector<LineColumn> positions;

  std::wregex pattern;
  try {
    pattern = std::wregex(options.search_query,
                          GetRegexTraits(options.case_sensitive));
  } catch (std::regex_error& e) {
    Error error(L"Regex failure: " + FromByteString(e.what()));
    progress_channel->Push(
        {.values = {{VersionPropertyKey(L"!"), error.read()}}});
    return error;
  }

  bool searched_every_line =
      contents.EveryLine([&](LineNumber position,
                             const NonNull<std::shared_ptr<const Line>>& line) {
        auto matches = GetMatches(line->ToString(), pattern);
        for (const auto& column : matches) {
          positions.push_back(LineColumn(position, column));
        }
        if (!matches.empty())
          progress_channel->Push(
              ProgressInformation{.counters = {{VersionPropertyKey(L"matches"),
                                                positions.size()}}});
        return !options.abort_value.has_value() &&
               (!options.required_positions.has_value() ||
                options.required_positions.value() > positions.size());
      });
  if (!searched_every_line)
    progress_channel->Push(
        ProgressInformation{.values = {{VersionPropertyKey(L"partial"), L""}}});
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

std::function<ValueOrError<SearchResultsSummary>()> BackgroundSearchCallback(
    SearchOptions search_options, const LineSequence& contents,
    ProgressChannel& progress_channel) {
  // TODO(easy, 2022-04-14): Why is this here?
  search_options.required_positions = 100;
  // Must take special care to only capture instances of thread-safe classes:
  return std::bind_front(
      [search_options, &progress_channel](
          LineSequence buffer_contents) -> ValueOrError<SearchResultsSummary> {
        ASSIGN_OR_RETURN(
            auto search_results,
            PerformSearch(search_options, buffer_contents, &progress_channel));
        return SearchResultsSummary{
            .matches = search_results.size(),
            .search_completion =
                search_results.size() >= kMatchesLimit
                    ? SearchResultsSummary::SearchCompletion::kInterrupted
                    : SearchResultsSummary::SearchCompletion::kFull};
      },
      std::move(contents));
}

std::wstring RegexEscape(NonNull<std::shared_ptr<LazyString>> str) {
  std::wstring results;
  static std::wstring literal_characters = L" ()<>{}+_-;\"':,?#%";
  ForEachColumn(str.value(), [&](ColumnNumber, wchar_t c) {
    if (!iswalnum(c) && literal_characters.find(c) == wstring::npos) {
      results.push_back('\\');
    }
    results.push_back(c);
  });
  return results;
}

PossibleError SearchInBuffer(PredictorInput& input, OpenBuffer& buffer,
                             std::set<std::wstring>& matches) {
  SearchOptions options;
  options.search_query = input.input;
  options.starting_position = buffer.position();

  ASSIGN_OR_RETURN(
      std::vector<LineColumn> positions,
      buffer.status().LogErrors(SearchHandler(
          input.editor.work_queue(), input.editor.modifiers().direction,
          options, buffer.contents().snapshot())));

  // Get the first kMatchesLimit matches:
  for (size_t i = 0; i < positions.size() && matches.size() < kMatchesLimit;
       i++) {
    auto position = positions[i];
    if (i == 0) {
      buffer.set_position(position);
    }
    CHECK_LT(position.line, buffer.EndLine());
    auto line = buffer.LineAt(position.line);
    CHECK_LT(position.column, line->EndColumn());
    matches.insert(RegexEscape(line->Substring(position.column)));
  }
  return Success();
}

futures::Value<PredictorOutput> SearchHandlerPredictor(PredictorInput input) {
  std::set<std::wstring> matches;
  for (gc::Root<OpenBuffer>& search_buffer : input.source_buffers) {
    SearchInBuffer(input, search_buffer.ptr().value(), matches);
  }
  if (!matches.empty()) {
    // Add the matches to the predictions buffer.
    for (auto& match : matches) {
      input.predictions.AppendToLastLine(NewLazyString(std::move(match)));
      input.predictions.AppendRawLine(MakeNonNullShared<Line>());
    }
  }
  input.predictions.EndOfFile();
  return input.predictions.WaitForEndOfFile().Transform(
      [](EmptyValue) { return PredictorOutput(); });
}

ValueOrError<std::vector<LineColumn>> SearchHandler(
    language::NonNull<std::shared_ptr<concurrent::WorkQueue>> work_queue,
    Direction direction, const SearchOptions& options,
    const LineSequence& contents) {
  if (options.search_query.empty()) {
    return {};
  }

  auto dummy_progress_channel = std::make_unique<ProgressChannel>(
      work_queue, [](ProgressInformation) {},
      WorkQueueChannelConsumeMode::kLastAvailable);
  ASSIGN_OR_RETURN(
      std::vector<LineColumn> results,
      PerformSearch(options, contents, dummy_progress_channel.get()));
  if (direction == Direction::kBackwards) {
    std::reverse(results.begin(), results.end());
  }

  vector<LineColumn> head;
  vector<LineColumn> tail;

  if (options.limit_position.has_value()) {
    Range range = {
        std::min(options.starting_position, options.limit_position.value()),
        std::max(options.starting_position, options.limit_position.value())};
    LOG(INFO) << "Removing elements outside of the range: " << range;
    std::vector<LineColumn> valid_candidates;
    for (auto& candidate : results) {
      if (range.Contains(candidate)) {
        valid_candidates.push_back(candidate);
      }
    }
    results = std::move(valid_candidates);
  }

  // Split them into head and tail depending on the current direction.
  for (auto& candidate : results) {
    bool use_head = true;
    switch (direction) {
      case Direction::kForwards:
        use_head = candidate > options.starting_position;
        break;
      case Direction::kBackwards:
        use_head = candidate < options.starting_position;
        break;
    }
    (use_head ? head : tail).push_back(candidate);
  }

  // Append the tail to the head.
  for (auto& candidate : tail) {
    head.push_back(candidate);
  }

  return head;
}

ValueOrError<LineColumn> GetNextMatch(
    language::NonNull<std::shared_ptr<concurrent::WorkQueue>> work_queue,
    Direction direction, const SearchOptions& options,
    const LineSequence& contents) {
  if (std::optional<std::vector<LineColumn>> results =
          OptionalFrom(SearchHandler(work_queue, direction, options, contents));
      results.has_value() && !results->empty())
    return results->at(0);

  // TODO(easy, 2023-09-09): Get rid of ToString.
  return Error(Append(NewLazyString(L"No matches: "),
                      NewLazyString(options.search_query))
                   ->ToString());
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
    wstring results_prefix(1 + static_cast<size_t>(log2(size)), L'🔍');
    buffer.status().SetInformationText(MakeNonNullShared<Line>(
        LineBuilder(Append(NewLazyString(results_prefix),
                           NewLazyString(L" Results: "),
                           NewLazyString(std::to_wstring(size))))
            .Build()));
  }
  vector<audio::Frequency> frequencies = {
      audio::Frequency(440.0), audio::Frequency(440.0),
      audio::Frequency(493.88), audio::Frequency(523.25),
      audio::Frequency(587.33)};
  frequencies.resize(std::min(frequencies.size(), size + 1),
                     audio::Frequency(0.0));
  audio::BeepFrequencies(buffer.editor().audio_player(), 0.1, frequencies);
  buffer.Set(buffer_variables::multiple_cursors, false);
}

}  // namespace afc::editor
