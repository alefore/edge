#include "search_handler.h"

#include <iostream>
#if CPP_REGEX
#include <regex>
#else
extern "C" {
#include <sys/types.h>
#include <regex.h>
}
#endif

#include "editor.h"

namespace {

using std::vector;
using std::string;

#if CPP_REGEX
typedef std::regex RegexPattern;
#else
typedef regex_t RegexPattern;
#endif

vector<size_t> GetMatches(const string& line, const RegexPattern& pattern) {
  int start = 0;
  vector<size_t> output;
  while (true) {
    size_t match = string::npos;
    string line_substr = line.substr(start);

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

    if (match == string::npos) { return output; }
    output.push_back(start + match);
    start += match + 1;
  }
}

template <typename Iterator>
size_t FindInterestingMatch(
    Iterator begin, Iterator end, bool wrapped, size_t pos_line,
    size_t pos_column, size_t line, afc::editor::Direction direction) {
  if (begin == end) { return string::npos; }
  if (pos_line != line) { return *begin; }
  bool require_greater = wrapped ^ (direction == afc::editor::FORWARDS);
  while (begin != end) {
    if (require_greater ? *begin > pos_column : *begin < pos_column) {
      return *begin;
    }
    ++begin;
  }
  return string::npos;
}

void PerformSearch(afc::editor::EditorState* editor_state,
                   const string& input) {
  using namespace afc::editor;
  auto buffer = editor_state->current_buffer()->second;

#if CPP_REGEX
  std::regex pattern(input);
#else
  regex_t pattern;
  regcomp(&pattern, input.c_str(), REG_ICASE);
#endif

  int delta;
  size_t position_line = buffer->current_position_line();
  assert(position_line < buffer->contents()->size());

  switch (editor_state->direction()) {
    case FORWARDS:
      delta = 1;
      break;
    case BACKWARDS:
      delta = -1;
      break;
  }

  editor_state->SetStatus("Not found");

  bool wrapped = false;

  // We search once for every line, and then again in the current line.
  for (size_t i = 0; i <= buffer->contents()->size(); i++) {
    string str = buffer->LineAt(position_line)->contents->ToString();

    vector<size_t> matches = GetMatches(str, pattern);
    size_t interesting_match;
    if (editor_state->direction() == FORWARDS) {
      interesting_match = FindInterestingMatch(
          matches.begin(), matches.end(), wrapped,
          buffer->current_position_line(), buffer->current_position_col(),
          position_line, editor_state->direction());
    } else {
      interesting_match = FindInterestingMatch(
          matches.rbegin(), matches.rend(), wrapped,
          buffer->current_position_line(), buffer->current_position_col(),
          position_line, editor_state->direction());
    }

    if (interesting_match != string::npos) {
      editor_state->PushCurrentPosition();
      buffer->set_current_position_line(position_line);
      buffer->set_current_position_col(interesting_match);
      editor_state->SetStatus(wrapped ? "Found (wrapped)" : "Found");
      break;  // TODO: Honor repetitions.
    }

    if (position_line == 0 && delta == -1) {
      position_line = buffer->contents()->size() - 1;
      wrapped = true;
    } else if (position_line == buffer->contents()->size() - 1 && delta == 1) {
      position_line = 0;
      wrapped = true;
    } else {
      position_line += delta;
    }
    assert(position_line < buffer->contents()->size());
  }
}

}  // namespace

namespace afc {
namespace editor {

#if CPP_REGEX
using std::regex;
#endif

void SearchHandlerPredictor(
    EditorState* editor_state, const string& input, OpenBuffer* buffer) {
  PerformSearch(editor_state, input);
  editor_state->set_status_prompt(false);
  editor_state->ScheduleRedraw();
}

void SearchHandler(const string& input, EditorState* editor_state) {
  editor_state->set_last_search_query(input);
  if (!editor_state->has_current_buffer() || input.empty()) {
    editor_state->ResetMode();
    editor_state->ResetStatus();
    editor_state->ScheduleRedraw();
    return;
  }

  PerformSearch(editor_state, input);

  editor_state->ResetMode();
  editor_state->ResetDirection();
  editor_state->ScheduleRedraw();
}

}  // namespace editor
}  // namespace afc
