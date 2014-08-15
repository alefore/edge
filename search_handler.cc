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
#include "substring.h"

namespace afc {
namespace editor {

#if CPP_REGEX
using std::regex;
#endif

void SearchHandler(const string& input, EditorState* editor_state) {
  if (editor_state->current_buffer == editor_state->buffers.end()
      || input.empty()
      || editor_state->get_current_buffer()->contents()->empty()) {
    editor_state->mode = NewCommandMode();
    editor_state->status = "";
    editor_state->screen_needs_redraw = true;
    return;
  }

  auto buffer = editor_state->get_current_buffer();

#if CPP_REGEX
  std::regex pattern(input);
  std::smatch pattern_match;
#else
  regex_t preg;
  regcomp(&preg, input.c_str(), REG_ICASE);
#endif

  int delta;
  size_t position_line = buffer->current_position_line();
  size_t position_col;
  shared_ptr<LazyString> next_line;

  assert(position_line < buffer->contents()->size());

  switch (editor_state->direction) {
    case FORWARDS:
      delta = 1;
      if (buffer->current_position_col() >= buffer->current_line()->contents->size()) {
        // We're at the end of the line, start searching in the next.
        position_line++;
        next_line = buffer->current_line()->contents;
        position_col = 0;
      } else {
        next_line = Substring(buffer->current_line()->contents,
                              buffer->current_position_col() + 1);
        position_col = buffer->current_position_col() + 1;
      }
      break;
    case BACKWARDS:
      delta = -1;
      // Even though we're searching backwards, we have to include the whole
      // line: it's possible there's a match starting before our current position
      // but going beyond it.  We have to compensate for that below, when
      // checking a match.
      next_line = buffer->current_line()->contents;
      position_col = 0;
      break;
  }

  editor_state->status = "Not found";

  bool wrapped = false;

  // This can certainly be optimized.
  for (size_t i = 0; i < buffer->contents()->size() + 1; i++) {
    string str = next_line->ToString();
    bool match = false;

#if CPP_REGEX
    std::regex_search(str, pattern_match, pattern);
    if (!pattern_match.empty()) {
      match = true;
    }
    size_t pos = pattern_match.prefix().first - str.begin();
#else
    regmatch_t matches;
    if (regexec(&preg, str.c_str(), 1, &matches, 0) == 0) {
      match = true;
    }
    size_t pos = matches.rm_so;
#endif

    if (match
        && editor_state->direction == BACKWARDS
        && position_line == buffer->current_position_line()
        && pos >= buffer->current_position_col()) {
      // Not a match we're interested on.
      match = false;
    }

    if (match) {
      buffer->set_current_position_line(position_line);
      buffer->set_current_position_col(pos + position_col);
      editor_state->status = wrapped ? "Found (wrapped)" : "Found";
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
    position_col = 0;
    next_line = buffer->contents()->at(position_line)->contents;
  }
  editor_state->mode = NewCommandMode();
  editor_state->direction = FORWARDS;
  editor_state->screen_needs_redraw = true;
}

}  // namespace editor
}  // namespace afc
