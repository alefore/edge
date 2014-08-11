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

namespace afc {
namespace editor {

#if CPP_REGEX
using std::regex;
#endif

void SearchHandler(const string& input, EditorState* editor_state) {
  if (editor_state->buffers.empty()) { return; }
  if (input.empty()) {
    editor_state->mode = NewCommandMode();
    editor_state->screen_needs_redraw = true;
    return;
  }

  std::cerr << "Search: [" << input << "]\n";
  auto buffer = editor_state->get_current_buffer();
#if CPP_REGEX
  std::regex pattern(input);
  std::smatch pattern_match;
#else
  regex_t preg;
  regcomp(&preg, input.c_str(), REG_ICASE);
#endif
  // This can certainly be optimized.
  for (size_t i = buffer->current_position_line + 1;
       i < buffer->contents.size();
       i++) {
    string str = buffer->contents[i]->contents->ToString();
    std::cerr << "String walked: [" << str << "]\n";
#if CPP_REGEX
    std::regex_search(str, pattern_match, pattern);
    if (pattern_match.empty()) {
      continue;
    }
    size_t pos = pattern_match.prefix().first - str.begin();
#else
    regmatch_t matches;
    if (regexec(&preg, str.c_str(), 1, &matches, 0) != 0) {
      continue;
    }
    size_t pos = matches.rm_so;
#endif
    buffer->current_position_line = i;
    buffer->current_position_col = pos;
    break;  // TODO: Honor repetitions.
  }
  editor_state->mode = NewCommandMode();
  editor_state->screen_needs_redraw = true;
}

}  // namespace editor
}  // namespace afc
