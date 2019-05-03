#include "src/search_handler.h"

#include <iostream>
#include <regex>
#include <set>

#include "src/audio.h"
#include "src/buffer_variables.h"
#include "src/char_buffer.h"
#include "src/editor.h"
#include "src/wstring.h"

namespace {

using namespace afc::editor;
using std::vector;
using std::wstring;

typedef std::wregex RegexPattern;

// Returns all columns where the current line matches the pattern.
vector<ColumnNumber> GetMatches(const wstring& line,
                                const RegexPattern& pattern) {
  size_t start = 0;
  vector<ColumnNumber> output;
  while (true) {
    size_t match = wstring::npos;
    wstring line_substr = line.substr(min(start, line.size()));

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
}

// Returns a vector with all positions matching input sorted in ascending order.
vector<LineColumn> PerformSearch(const SearchOptions& options,
                                 OpenBuffer* buffer) {
  using namespace afc::editor;
  vector<LineColumn> positions;

  auto traits = std::regex_constants::extended;
  if (!options.case_sensitive) {
    traits |= std::regex_constants::icase;
  }
  std::wregex pattern;
  try {
    pattern = std::wregex(options.search_query, traits);
  } catch (std::regex_error& e) {
    buffer->status()->SetWarningText(L"Regex failure: " +
                                     FromByteString(e.what()));
    return {};
  }

  buffer->contents()->EveryLine(
      [&positions, &pattern](LineNumber position, const Line& line) {
        for (const auto& column : GetMatches(line.ToString(), pattern)) {
          positions.push_back(LineColumn(position, column));
        }
        return true;
      });
  return positions;
}

}  // namespace

namespace afc {
namespace editor {

wstring RegexEscape(shared_ptr<LazyString> str) {
  wstring results;
  static wstring literal_characters = L" ()<>{}+_-;\"':,?#%";
  for (size_t i = 0; i < str->size(); i++) {
    wchar_t c = str->get(i);
    if (!iswalnum(c) && literal_characters.find(c) == wstring::npos) {
      results.push_back('\\');
    }
    results.push_back(c);
  }
  return results;
}

// Returns all matches starting at start. If end is not nullptr, only matches
// in the region enclosed by start and *end will be returned.
vector<LineColumn> PerformSearchWithDirection(EditorState* editor_state,
                                              const SearchOptions& options) {
  auto buffer = editor_state->current_buffer();
  auto direction = editor_state->modifiers().direction;
  vector<LineColumn> candidates = PerformSearch(options, buffer.get());
  if (direction == BACKWARDS) {
    std::reverse(candidates.begin(), candidates.end());
  }

  vector<LineColumn> head;
  vector<LineColumn> tail;

  if (options.limit_position.has_value()) {
    Range range = {
        min(options.starting_position, options.limit_position.value()),
        max(options.starting_position, options.limit_position.value())};
    LOG(INFO) << "Removing elements outside of the range: " << range;
    vector<LineColumn> valid_candidates;
    for (auto& candidate : candidates) {
      if (range.Contains(candidate)) {
        valid_candidates.push_back(candidate);
      }
    }
    candidates = std::move(valid_candidates);
  }

  // Split them into head and tail depending on the current direction.
  for (auto& candidate : candidates) {
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
    buffer->status()->SetInformationText(L"🔍 No results.");
    BeepFrequencies(editor_state->audio_player(), {523.25, 261.63, 261.63});
  } else {
    if (head.size() == 1) {
      buffer->status()->SetInformationText(L"🔍 1 result.");
    } else {
      wstring results_prefix(1 + static_cast<size_t>(log2(head.size())), L'🔍');
      buffer->status()->SetInformationText(results_prefix + L" Results: " +
                                           std::to_wstring(head.size()));
    }
    vector<double> frequencies = {261.63, 329.63, 392.0, 523.25, 659.25};
    frequencies.resize(min(frequencies.size(), head.size() + 1));
    BeepFrequencies(editor_state->audio_player(), frequencies);
    buffer->Set(buffer_variables::multiple_cursors, false);
  }
  return head;
}

void SearchHandlerPredictor(EditorState* editor_state, const wstring& input,
                            OpenBuffer* predictions_buffer) {
  auto buffer = editor_state->current_buffer();
  SearchOptions options;
  options.search_query = input;
  options.case_sensitive =
      buffer->Read(buffer_variables::search_case_sensitive);
  options.starting_position = buffer->position();
  auto positions = PerformSearchWithDirection(editor_state, options);

  // Get the first kMatchesLimit matches:
  const int kMatchesLimit = 100;
  std::set<wstring> matches;
  for (size_t i = 0; i < positions.size() && matches.size() < kMatchesLimit;
       i++) {
    if (i == 0) {
      buffer->set_position(positions[0]);
      buffer->status()->Reset();
      editor_state->ScheduleRedraw();
    }
    matches.insert(RegexEscape(
        buffer->LineAt(positions[i].line)->Substring(positions[i].column)));
  }

  // Add the matches to the predictions buffer.
  for (auto& match : matches) {
    predictions_buffer->AppendToLastLine(NewLazyString(std::move(match)));
    predictions_buffer->AppendRawLine(std::make_shared<Line>(Line::Options()));
  }
  predictions_buffer->EndOfFile();
}

vector<LineColumn> SearchHandler(EditorState* editor_state,
                                 const SearchOptions& options) {
  editor_state->set_last_search_query(options.search_query);
  if (!editor_state->has_current_buffer() || options.search_query.empty()) {
    return {};
  }

  return PerformSearchWithDirection(editor_state, options);
}

void JumpToNextMatch(EditorState* editor_state, const SearchOptions& options) {
  auto results = SearchHandler(editor_state, options);
  auto buffer = editor_state->current_buffer();
  CHECK(buffer != nullptr);
  if (results.empty()) {
    buffer->status()->SetInformationText(L"No matches: " +
                                         options.search_query);
  } else {
    buffer->set_position(results[0]);
    editor_state->PushCurrentPosition();
  }
}

}  // namespace editor
}  // namespace afc
