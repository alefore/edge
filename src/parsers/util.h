#include "src/parse_tools.h"

namespace afc::editor::parsers {
// `result` should be after the initial double-quoted string.
void ParseDoubleQuotedString(
    ParseData* result,
    infrastructure::screen::LineModifierSet string_modifiers);
}  // namespace afc::editor::parsers
