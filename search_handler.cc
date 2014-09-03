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

#include "char_buffer.h"
#include "editor.h"

namespace {

using namespace afc::editor;
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
    Iterator begin, Iterator end, bool wrapped, const LineColumn start_position,
    size_t line, afc::editor::Direction direction) {
  if (begin == end) { return string::npos; }
  if (start_position.line != line) { return *begin; }
  bool require_greater = wrapped ^ (direction == afc::editor::FORWARDS);
  while (begin != end) {
    if (require_greater
        ? *begin > start_position.column
        : *begin < start_position.column) {
      return *begin;
    }
    ++begin;
  }
  return string::npos;
}

bool PerformSearch(
    const string& input,
    OpenBuffer* buffer,
    const LineColumn& start_position,
    afc::editor::Direction direction,
    LineColumn* match_position,
    bool* wrapped) {
  using namespace afc::editor;

#if CPP_REGEX
  std::regex pattern(input);
#else
  regex_t pattern;
  if (regcomp(&pattern, input.c_str(), REG_ICASE) != 0) {
    return false;
  }
#endif

  int delta;
  size_t position_line = start_position.line;
  assert(position_line < buffer->contents()->size());

  switch (direction) {
    case FORWARDS:
      delta = 1;
      break;
    case BACKWARDS:
      delta = -1;
      break;
  }

  *wrapped = false;

  // We search once for every line, and then again in the current line.
  for (size_t i = 0; i <= buffer->contents()->size(); i++) {
    string str = buffer->LineAt(position_line)->contents->ToString();

    vector<size_t> matches = GetMatches(str, pattern);
    size_t interesting_match;
    if (direction == FORWARDS) {
      interesting_match = FindInterestingMatch(
          matches.begin(), matches.end(), *wrapped, start_position,
          position_line, direction);
    } else {
      interesting_match = FindInterestingMatch(
          matches.rbegin(), matches.rend(), *wrapped, start_position,
          position_line, direction);
    }

    if (interesting_match != string::npos) {
      match_position->line = position_line;
      match_position->column = interesting_match;
      return true;
    }

    if (position_line == 0 && delta == -1) {
      position_line = buffer->contents()->size() - 1;
      *wrapped = true;
    } else if (position_line == buffer->contents()->size() - 1 && delta == 1) {
      position_line = 0;
      *wrapped = true;
    } else {
      position_line += delta;
    }
    assert(position_line < buffer->contents()->size());
  }
  return false;
}

}  // namespace

namespace afc {
namespace editor {

#if CPP_REGEX
using std::regex;
#endif

shared_ptr<LazyString>
RegexEscape(shared_ptr<LazyString> str) {
  string results;
  static string literal_characters = " ()<>";
  for (size_t i = 0; i < str->size(); i++) {
    int c = str->get(i);
    if (!isalnum(c) && literal_characters.find(c) == string::npos) {
      results.push_back('\\');
    }
    results.push_back(c);
  }
  return NewCopyString(results);
}

void SearchHandlerPredictor(
    EditorState* editor_state, const string& input,
    OpenBuffer* predictions_buffer) {
  auto buffer = editor_state->current_buffer()->second;
  LineColumn match_position = buffer->position();
  bool already_wrapped = false;
  for (size_t i = 0; i < 10; i++) {
    bool wrapped;
    if (!PerformSearch(input, buffer.get(), match_position,
                       editor_state->direction(), &match_position, &wrapped)) {
      break;
    }
    if (i == 0) {
      buffer->set_position(match_position);
      editor_state->set_status_prompt(false);
      editor_state->ScheduleRedraw();
    }
    predictions_buffer->AppendLine(
        editor_state,
        RegexEscape(
            Substring(buffer->LineAt(match_position.line)->contents,
                      match_position.column)));
    if (wrapped && already_wrapped) {
      break;
    }
    already_wrapped |= wrapped;
  }
  predictions_buffer->EndOfFile(editor_state);
}

void SearchHandler(
    const LineColumn& starting_position, const string& input,
    EditorState* editor_state) {
  editor_state->set_last_search_query(input);
  if (!editor_state->has_current_buffer() || input.empty()) {
    editor_state->ResetMode();
    editor_state->ResetStatus();
    editor_state->ScheduleRedraw();
    return;
  }

  auto buffer = editor_state->current_buffer()->second;
  if (starting_position != buffer->position()) {
    // The user must have used the predictor, which probably means we don't need
    // to do much.
    editor_state->ResetMode();
    editor_state->ResetDirection();
    return;
  }

  LineColumn match_position;
  bool wrapped;
  if (!PerformSearch(input, buffer.get(), buffer->position(),
                     editor_state->direction(), &match_position, &wrapped)) {
    editor_state->SetStatus("No matches: " + input);
  } else {
    buffer->set_position(match_position);
    editor_state->PushCurrentPosition();
    if (wrapped) {
      editor_state->SetStatus("Found (wrapped).");
    } else {
      editor_state->SetStatus("Found.");
    }
  }

  editor_state->ResetMode();
  editor_state->ResetDirection();
  editor_state->ScheduleRedraw();
}

}  // namespace editor
}  // namespace afc
