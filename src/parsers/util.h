#include "src/parse_tools.h"

namespace afc::editor::parsers {
// `result` should be after the initial double-quoted string.
void ParseDoubleQuotedString(
    ParseData* result, infrastructure::screen::LineModifierSet string_modifiers,
    std::unordered_set<ParseTreeProperty> properties);

// `result` should be after the initial digit.
void ParseNumber(ParseData* result,
                 infrastructure::screen::LineModifierSet number_modifiers,
                 std::unordered_set<ParseTreeProperty> properties);
}  // namespace afc::editor::parsers
