#include "src/search_handler.h"

#include <iostream>
#include <regex>
#include <set>

#include "src/audio.h"
#include "src/buffer_variables.h"
#include "src/char_buffer.h"
#include "src/concurrent/notification.h"
#include "src/editor.h"
#include "src/language/wstring.h"
#include "src/lazy_string_functional.h"

namespace afc::editor {
namespace {

using std::vector;
using std::wstring;

using concurrent::WorkQueueChannelConsumeMode;
using language::EmptyValue;
using language::Error;
using language::FromByteString;
using language::Success;
using language::ValueOrError;

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

struct SearchResults {
  std::optional<std::wstring> error;
  // A vector with all positions matching input sorted in ascending order.
  std::vector<LineColumn> positions;
};

auto GetRegexTraits(const OpenBuffer& buffer) {
  auto traits = std::regex_constants::extended;
  if (!buffer.Read(buffer_variables::search_case_sensitive)) {
    traits |= std::regex_constants::icase;
  }
  return traits;
}

template <typename RegexTraits>
SearchResults PerformSearch(const SearchOptions& options, RegexTraits traits,
                            const BufferContents& contents,
                            ProgressChannel* progress_channel) {
  vector<LineColumn> positions;

  std::wregex pattern;
  try {
    pattern = std::wregex(options.search_query, traits);
  } catch (std::regex_error& e) {
    SearchResults output;
    output.error = L"Regex failure: " + FromByteString(e.what());
    progress_channel->Push({.values = {{StatusPromptExtraInformationKey(L"!"),
                                        output.error.value()}}});
    return output;
  }

  SearchResults output;

  bool searched_every_line =
      contents.EveryLine([&](LineNumber position, const Line& line) {
        auto matches = GetMatches(line.ToString(), pattern);
        for (const auto& column : matches) {
          output.positions.push_back(LineColumn(position, column));
        }
        if (!matches.empty())
          progress_channel->Push(ProgressInformation{
              .counters = {{StatusPromptExtraInformationKey(L"matches"),
                            output.positions.size()}}});
        return !options.abort_notification->HasBeenNotified() &&
               (!options.required_positions.has_value() ||
                options.required_positions.value() > output.positions.size());
      });
  if (!searched_every_line)
    progress_channel->Push(ProgressInformation{
        .values = {{StatusPromptExtraInformationKey(L"partial"), L""}}});
  VLOG(5) << "Perform search found matches: " << output.positions.size();
  return output;
}

}  // namespace

std::function<ValueOrError<SearchResultsSummary>()> BackgroundSearchCallback(
    SearchOptions search_options, const OpenBuffer& buffer,
    ProgressChannel& progress_channel) {
  // TODO(easy, 2022-04-14): Why is this here?
  search_options.required_positions = 100;
  auto traits = GetRegexTraits(buffer);
  std::shared_ptr<BufferContents> buffer_contents = buffer.contents().copy();
  // Must take special care to only capture instances of thread-safe classes:
  return [search_options, traits, buffer_contents,
          &progress_channel]() -> ValueOrError<SearchResultsSummary> {
    auto search_results = PerformSearch(search_options, traits,
                                        *buffer_contents, &progress_channel);
    VLOG(5) << "Background search completed for \""
            << search_options.search_query
            << "\", found results: " << search_results.positions.size();
    if (search_results.error.has_value()) {
      return Error(search_results.error.value());
    }
    return Success(SearchResultsSummary{
        .matches = search_results.positions.size(),
        .search_completion =
            search_results.positions.size() >= kMatchesLimit
                ? SearchResultsSummary::SearchCompletion::kInterrupted
                : SearchResultsSummary::SearchCompletion::kFull});
  };
}

std::wstring RegexEscape(std::shared_ptr<LazyString> str) {
  std::wstring results;
  static std::wstring literal_characters = L" ()<>{}+_-;\"':,?#%";
  ForEachColumn(*str, [&](ColumnNumber, wchar_t c) {
    if (!iswalnum(c) && literal_characters.find(c) == wstring::npos) {
      results.push_back('\\');
    }
    results.push_back(c);
  });
  return results;
}

// Returns all matches starting at start. If end is not nullptr, only matches
// in the region enclosed by start and *end will be returned.
ValueOrError<std::vector<LineColumn>> PerformSearchWithDirection(
    EditorState& editor_state, const SearchOptions& options,
    OpenBuffer& buffer) {
  auto direction = editor_state.modifiers().direction;
  auto dummy_progress_channel = std::make_unique<ProgressChannel>(
      editor_state.work_queue(), [](ProgressInformation) {},
      WorkQueueChannelConsumeMode::kLastAvailable);
  SearchResults results =
      PerformSearch(options, GetRegexTraits(buffer), buffer.contents(),
                    dummy_progress_channel.get());
  if (results.error.has_value()) {
    return Error(results.error.value());
  }
  if (direction == Direction::kBackwards) {
    std::reverse(results.positions.begin(), results.positions.end());
  }

  vector<LineColumn> head;
  vector<LineColumn> tail;

  if (options.limit_position.has_value()) {
    Range range = {
        min(options.starting_position, options.limit_position.value()),
        max(options.starting_position, options.limit_position.value())};
    LOG(INFO) << "Removing elements outside of the range: " << range;
    vector<LineColumn> valid_candidates;
    for (auto& candidate : results.positions) {
      if (range.Contains(candidate)) {
        valid_candidates.push_back(candidate);
      }
    }
    results.positions = std::move(valid_candidates);
  }

  // Split them into head and tail depending on the current direction.
  for (auto& candidate : results.positions) {
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

  if (head.empty()) {
    buffer.status().SetInformationText(L"🔍 No results.");
    audio::BeepFrequencies(editor_state.audio_player(), 0.1,
                           {audio::Frequency(659.25), audio::Frequency(440.0),
                            audio::Frequency(440.0)});
  } else {
    if (head.size() == 1) {
      buffer.status().SetInformationText(L"🔍 1 result.");
    } else {
      wstring results_prefix(1 + static_cast<size_t>(log2(head.size())), L'🔍');
      buffer.status().SetInformationText(results_prefix + L" Results: " +
                                         std::to_wstring(head.size()));
    }
    vector<audio::Frequency> frequencies = {
        audio::Frequency(440.0), audio::Frequency(440.0),
        audio::Frequency(493.88), audio::Frequency(523.25),
        audio::Frequency(587.33)};
    frequencies.resize(min(frequencies.size(), head.size() + 1),
                       audio::Frequency(0.0));
    audio::BeepFrequencies(editor_state.audio_player(), 0.1, frequencies);
    buffer.Set(buffer_variables::multiple_cursors, false);
  }
  return Success(head);
}

futures::Value<PredictorOutput> SearchHandlerPredictor(PredictorInput input) {
  CHECK(input.predictions != nullptr);
  std::set<wstring> matches;
  for (auto& search_buffer : input.source_buffers) {
    CHECK(search_buffer != nullptr);
    SearchOptions options;
    options.search_query = input.input;
    options.starting_position = search_buffer->position();
    auto positions =
        PerformSearchWithDirection(input.editor, options, *search_buffer);
    if (positions.IsError()) {
      search_buffer->status().SetWarningText(positions.error().description);
      continue;
    }

    // Get the first kMatchesLimit matches:
    for (size_t i = 0;
         i < positions.value().size() && matches.size() < kMatchesLimit; i++) {
      auto position = positions.value()[i];
      if (i == 0) {
        search_buffer->set_position(position);
      }
      CHECK_LT(position.line, search_buffer->EndLine());
      auto line = search_buffer->LineAt(position.line);
      CHECK_LT(position.column, line->EndColumn());
      matches.insert(RegexEscape(line->Substring(position.column)));
    }
  }
  if (!matches.empty()) {
    // Add the matches to the predictions buffer.
    for (auto& match : matches) {
      input.predictions->AppendToLastLine(NewLazyString(std::move(match)));
      input.predictions->AppendRawLine(std::make_shared<Line>(Line::Options()));
    }
  }
  input.predictions->EndOfFile();
  return input.predictions->WaitForEndOfFile().Transform(
      [](EmptyValue) { return PredictorOutput(); });
}

vector<LineColumn> SearchHandler(EditorState& editor_state,
                                 const SearchOptions& options,
                                 OpenBuffer& buffer) {
  if (!editor_state.has_current_buffer() || options.search_query.empty()) {
    return {};
  }

  auto output = PerformSearchWithDirection(editor_state, options, buffer);
  if (!output.IsError() && output.value().empty() &&
      buffer.Read(buffer_variables::search_filter_buffer)) {
    buffer.editor().CloseBuffer(buffer);
    return {};
  } else {
    return buffer.status().ConsumeErrors(output, {});
  }
}

void JumpToNextMatch(EditorState& editor_state, const SearchOptions& options,
                     OpenBuffer& buffer) {
  auto results = SearchHandler(editor_state, options, buffer);
  if (results.empty()) {
    buffer.status().SetInformationText(L"No matches: " + options.search_query);
  } else {
    buffer.set_position(results[0]);
    editor_state.PushCurrentPosition();
  }
}

}  // namespace afc::editor
