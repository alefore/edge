#include "src/search_handler.h"

#include <iostream>
#include <regex>
#include <set>

#include "src/audio.h"
#include "src/buffer_variables.h"
#include "src/char_buffer.h"
#include "src/editor.h"
#include "src/lazy_string_functional.h"
#include "src/wstring.h"

namespace afc::editor {
namespace {

using std::vector;
using std::wstring;

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

SearchResults PerformSearch(const SearchOptions& options,
                            const BufferContents& contents) {
  vector<LineColumn> positions;

  auto traits = std::regex_constants::extended;
  if (!options.case_sensitive) {
    traits |= std::regex_constants::icase;
  }
  std::wregex pattern;
  try {
    pattern = std::wregex(options.search_query, traits);
  } catch (std::regex_error& e) {
    SearchResults output;
    output.error = L"Regex failure: " + FromByteString(e.what());
    return output;
  }

  SearchResults output;

  contents.EveryLine([&](LineNumber position, const Line& line) {
    for (const auto& column : GetMatches(line.ToString(), pattern)) {
      output.positions.push_back(LineColumn(position, column));
    }
    return !options.required_positions.has_value() ||
           options.required_positions.value() > output.positions.size();
  });
  return output;
}

AsyncSearchOutput DoAsyncSearch(AsyncSearchInput input) {
  CHECK(input.buffer != nullptr);
  input.search_options.required_positions = 2;
  auto search_results = PerformSearch(input.search_options, *input.buffer);
  VLOG(5) << "Async search completed for \""
          << input.search_options.search_query
          << "\", found results: " << search_results.positions.size();
  AsyncSearchOutput output;
  if (search_results.error.has_value()) {
    output.results = AsyncSearchOutput::Results::kInvalidPattern;
  } else if (search_results.positions.empty()) {
    output.results = AsyncSearchOutput::Results::kNoMatches;
  } else if (search_results.positions.size() == 1) {
    output.results = AsyncSearchOutput::Results::kOneMatch;
  } else {
    output.results = AsyncSearchOutput::Results::kManyMatches;
  }
  input.callback(output);
  return output;
}

}  // namespace

std::unique_ptr<AsyncProcessor<AsyncSearchInput, AsyncSearchOutput>>
NewAsyncSearchProcessor() {
  AsyncProcessor<AsyncSearchInput, AsyncSearchOutput>::Options options;
  options.factory = DoAsyncSearch;
  return std::make_unique<AsyncProcessor<AsyncSearchInput, AsyncSearchOutput>>(
      std::move(options));
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
std::optional<std::vector<LineColumn>> PerformSearchWithDirection(
    EditorState* editor_state, const SearchOptions& options) {
  auto direction = editor_state->modifiers().direction;
  SearchResults results = PerformSearch(options, *options.buffer->contents());
  if (results.error.has_value()) {
    options.buffer->status()->SetWarningText(results.error.value());
    return std::nullopt;
  }
  if (direction == BACKWARDS) {
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
    ((direction == FORWARDS ? candidate > options.starting_position
                            : candidate < options.starting_position)
         ? head
         : tail)
        .push_back(candidate);
  }

  // Append the tail to the head.
  for (auto& candidate : tail) {
    head.push_back(candidate);
  }

  if (head.empty()) {
    options.buffer->status()->SetInformationText(L"🔍 No results.");
    BeepFrequencies(editor_state->audio_player(), {523.25, 261.63, 261.63});
  } else {
    if (head.size() == 1) {
      options.buffer->status()->SetInformationText(L"🔍 1 result.");
    } else {
      wstring results_prefix(1 + static_cast<size_t>(log2(head.size())), L'🔍');
      options.buffer->status()->SetInformationText(
          results_prefix + L" Results: " + std::to_wstring(head.size()));
    }
    vector<double> frequencies = {261.63, 329.63, 392.0, 523.25, 659.25};
    frequencies.resize(min(frequencies.size(), head.size() + 1));
    BeepFrequencies(editor_state->audio_player(), frequencies);
    options.buffer->Set(buffer_variables::multiple_cursors, false);
  }
  return head;
}

void SearchHandlerPredictor(PredictorInput input) {
  auto search_buffer = input.source_buffer;
  CHECK(search_buffer != nullptr);
  CHECK(input.predictions != nullptr);
  CHECK(input.predictions->status()->prompt_buffer() != nullptr);
  SearchOptions options;
  options.buffer = search_buffer.get();
  options.search_query = input.input;
  options.case_sensitive =
      search_buffer->Read(buffer_variables::search_case_sensitive);
  options.starting_position = search_buffer->position();
  auto positions = PerformSearchWithDirection(input.editor, options);
  if (!positions.has_value()) {
    input.predictions->EndOfFile();
    input.predictions->AddEndOfFileObserver(input.callback);
    return;
  }

  // Get the first kMatchesLimit matches:
  const int kMatchesLimit = 100;
  std::set<wstring> matches;
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

  // Add the matches to the predictions buffer.
  for (auto& match : matches) {
    input.predictions->AppendToLastLine(NewLazyString(std::move(match)));
    input.predictions->AppendRawLine(std::make_shared<Line>(Line::Options()));
  }
  input.predictions->EndOfFile();
  input.predictions->AddEndOfFileObserver(input.callback);
}

vector<LineColumn> SearchHandler(EditorState* editor_state,
                                 const SearchOptions& options) {
  editor_state->set_last_search_query(options.search_query);
  if (!editor_state->has_current_buffer() || options.search_query.empty()) {
    return {};
  }

  return PerformSearchWithDirection(editor_state, options)
      .value_or(std::vector<LineColumn>());
}

void JumpToNextMatch(EditorState* editor_state, const SearchOptions& options) {
  auto results = SearchHandler(editor_state, options);
  CHECK(options.buffer != nullptr);
  if (results.empty()) {
    options.buffer->status()->SetInformationText(L"No matches: " +
                                                 options.search_query);
  } else {
    options.buffer->set_position(results[0]);
    editor_state->PushCurrentPosition();
  }
}

}  // namespace afc::editor
