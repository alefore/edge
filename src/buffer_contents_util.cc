#include "src/buffer_contents_util.h"
namespace afc::editor {
std::wstring GetCurrentToken(CurrentTokenOptions options) {
  auto line = options.contents.at(options.line_column.line)->ToString();
  // Scroll back to the first character outside of the token. If we're in not
  // inside a token, this is a no-op.
  size_t start = line.find_last_not_of(options.token_characters,
                                       options.line_column.column.column);
  if (start == line.npos) {
    start = 0;
  }

  // Scroll past any non-token characters. Typically this will just skip the
  // character we landed at in the block above. However, if we started in a
  // sequence of non-token characters, we skip them all.
  start = line.find_first_of(options.token_characters, start);
  if (start != line.npos) {
    line = line.substr(start);
  }

  size_t end = line.find_first_not_of(options.token_characters);
  if (end != line.npos) {
    line = line.substr(0, end);
  }

  return line;
}
}  // namespace afc::editor
