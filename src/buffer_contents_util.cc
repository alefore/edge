#include "src/buffer_contents_util.h"

using afc::language::lazy_string::LazyString;

namespace afc::editor {
LazyString GetCurrentToken(CurrentTokenOptions options) {
  std::wstring line = options.contents.at(options.line_column.line).ToString();
  // Scroll back to the first character outside of the token. If we're in not
  // inside a token, this is a no-op.
  size_t start = line.find_last_not_of(options.token_characters,
                                       options.line_column.column.read());
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

  // TODO(trivial, 2024-08-04): Define line directly as LazyString.
  return LazyString{line};
}
}  // namespace afc::editor
