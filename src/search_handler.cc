#include "search_handler.h"

#include <set>

#include <iostream>
#if CPP_REGEX
#include <regex>
#else
extern "C" {
#include <sys/types.h>
#include <regex.h>
}
#endif

#include "char_buffer.h"
#include "editor.h"
#include "wstring.h"

namespace {

using namespace afc::editor;
using std::vector;
using std::wstring;

#if CPP_REGEX
typedef std::regex RegexPattern;
#else
typedef regex_t RegexPattern;
#endif

// Returns all columns where the current line matches the pattern.
vector<size_t> GetMatches(const wstring& line, const RegexPattern& pattern) {
  size_t start = 0;
  vector<size_t> output;
  while (true) {
    size_t match = wstring::npos;
    // TODO: Ugh, our regexp engines are not wchar aware. :-(
    string line_substr = ToByteString(line.substr(min(start, line.size())));

#if CPP_REGEX
    std::smatch pattern_match;
    std::regex_search(line_substr, pattern_match, pattern);
    if (!pattern_match.empty()) {
      match = pattern_match.prefix().first - line_substr.begin();
    }
#else
    regmatch_t matches;
    if (regexec(&pattern, line_substr.c_str(), 1, &matches, 0) == 0) {
      match = matches.rm_so;
    }
#endif

    if (match == wstring::npos) { return output; }
    output.push_back(start + match);
    start += match + 1;
  }
}

// Returns a vector with all positions matching input sorted in ascending order.
vector<LineColumn> PerformSearch(const wstring& input, OpenBuffer* buffer) {
  using namespace afc::editor;
  vector<LineColumn> positions;

#if CPP_REGEX
  // TODO: Get rid of ToByteString. Ugh.
  std::regex pattern(ToByteString(input));
#else
  regex_t pattern;
  // TODO: Get rid of ToByteString. Ugh.
  if (regcomp(&pattern, ToByteString(input).c_str(), REG_ICASE) != 0) {
    return positions;
  }
#endif

  const auto first = buffer->contents()->begin();
  for (auto line = first; line != buffer->contents()->end(); ++line) {
    for (const auto& column : GetMatches((*line)->ToString(), pattern)) {
      positions.push_back(LineColumn(line - first, column));
    }
  }
  return positions;
}

}  // namespace

namespace afc {
namespace editor {

#if CPP_REGEX
using std::regex;
#endif

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
vector<LineColumn> PerformSearchWithDirection(
    EditorState* editor_state, const wstring& input, LineColumn start,
    const LineColumn* end) {
  auto buffer = editor_state->current_buffer()->second;
  auto direction = editor_state->modifiers().direction;
  vector<LineColumn> candidates = PerformSearch(input, buffer.get());
  if (direction == BACKWARDS) {
    std::reverse(candidates.begin(), candidates.end());
  }

  vector<LineColumn> head;
  vector<LineColumn> tail;

  if (end != nullptr) {
    LOG(INFO) << "Removing elements outside of the range: " << min(start, *end)
              << " to " << max(start, *end);
    vector<LineColumn> valid_candidates;
    for (auto& candidate : candidates) {
      if (candidate >= min(start, *end) && candidate < max(start, *end)) {
        valid_candidates.push_back(candidate);
      }
    }
    candidates = std::move(valid_candidates);
  }

  // Split them into head and tail depending on the current direction.
  for (auto& candidate : candidates) {
    ((direction == FORWARDS ? candidate > start : candidate < start)
         ? head : tail)
        .push_back(candidate);
  }

  // Append the tail to the head.
  for (auto& candidate : tail) {
    head.push_back(candidate);
  }

  return head;
}

void SearchHandlerPredictor(
    EditorState* editor_state, const wstring& input,
    OpenBuffer* predictions_buffer) {
  auto buffer = editor_state->current_buffer()->second;
  auto positions = PerformSearchWithDirection(editor_state, input,
      buffer->position(), nullptr);

  // Get the first kMatchesLimit matches:
  const int kMatchesLimit = 100;
  std::set<wstring> matches;
  for (size_t i = 0; i < positions.size() && matches.size() < kMatchesLimit; i++) {
    if (i == 0) {
      buffer->set_position(positions[0]);
      editor_state->set_status_prompt(false);
      editor_state->ScheduleRedraw();
    }
    matches.insert(RegexEscape(
        buffer->LineAt(positions[i].line)->Substring(positions[i].column)));
  }

  // Add the matches to the predictions buffer.
  for (auto& match : matches) {
    predictions_buffer->AppendLine(editor_state, NewCopyString(match));
  }
  predictions_buffer->EndOfFile(editor_state);
}

vector<LineColumn> SearchHandler(
    EditorState* editor_state, const SearchOptions& options) {
  editor_state->set_last_search_query(options.search_query);
  if (!editor_state->has_current_buffer() || options.search_query.empty()) {
    return vector<LineColumn>();
  }

  auto results = PerformSearchWithDirection(
      editor_state, options.search_query, options.starting_position,
      options.has_limit_position ? &options.limit_position : nullptr);
  if (results.empty()) {
    editor_state->SetStatus(L"No matches: " + options.search_query);
  } else {
    editor_state->current_buffer()->second->set_position(results[0]);
    editor_state->PushCurrentPosition();
  }
  return results;
}

}  // namespace editor
}  // namespace afc
