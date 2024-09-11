#include "src/buffer_contents_util.h"

#include "src/language/lazy_string/functional.h"

using afc::language::lazy_string::ColumnNumber;
using afc::language::lazy_string::FindFirstOf;
using afc::language::lazy_string::FindLastNotOf;
using afc::language::lazy_string::LazyString;

namespace afc::editor {
LazyString GetCurrentToken(CurrentTokenOptions options) {
  LazyString line =
      options.contents.at(options.line_column.line).contents().read();
  // Scroll back to the first character outside of the token. If we're in not
  // inside a token, this is a no-op.
  line = line.Substring(
      FindLastNotOf(
          line.Substring(ColumnNumber{}, options.line_column.column.ToDelta()),
          options.token_characters)
          .value_or(ColumnNumber{}));

  // Scroll past any non-token characters. Typically this will just skip the
  // character we landed at in the block above. However, if we started in a
  // sequence of non-token characters, we skip them all.
  if (std::optional<ColumnNumber> start =
          FindFirstOf(line, options.token_characters);
      start.has_value())
    line = line.Substring(*start);

  if (std::optional<ColumnNumber> end =
          FindFirstNotOf(line, options.token_characters);
      end.has_value())
    line = line.Substring(ColumnNumber{}, end->ToDelta());

  return line;
}
}  // namespace afc::editor
