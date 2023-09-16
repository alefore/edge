#include "src/parsers/util.h"

namespace afc::editor::parsers {
using infrastructure::screen::LineModifier;
using infrastructure::screen::LineModifierSet;
using language::lazy_string::ColumnNumber;
using language::lazy_string::ColumnNumberDelta;
using language::text::LineColumn;

// TODO(easy, 2023-09-16): Reuse these symbosl in cpp_parse_tree.
static const LineModifierSet BAD_PARSE_MODIFIERS =
    LineModifierSet({LineModifier::kBgRed, LineModifier::kBold});

static const std::wstring digit_chars = L"1234567890.";

void ParseDoubleQuotedString(ParseData* result,
                             LineModifierSet string_modifiers,
                             std::unordered_set<ParseTreeProperty> properties) {
  LineColumn original_position = result->position();
  CHECK_GT(original_position.column, ColumnNumber(0));

  Seek seek = result->seek();
  while (seek.read() != L'"' && seek.read() != L'\n' && !seek.AtRangeEnd()) {
    if (seek.read() == '\\') {
      seek.Once();
    }
    seek.Once();
  }
  if (seek.read() == L'"') {
    seek.Once();
    CHECK_EQ(result->position().line, original_position.line);
    result->PushAndPop(result->position().column - original_position.column +
                           ColumnNumberDelta(1),
                       string_modifiers, properties);
    // TODO: words_parser_->FindChildren(result->buffer(), tree);
  } else {
    result->set_position(original_position);
    result->PushAndPop(ColumnNumberDelta(1), BAD_PARSE_MODIFIERS);
  }
}

void ParseNumber(ParseData* result, LineModifierSet number_modifiers,
                 std::unordered_set<ParseTreeProperty> properties) {
  CHECK_GE(result->position().column, ColumnNumber(1));
  LineColumn original_position = result->position();
  original_position.column--;

  result->seek().UntilCurrentCharNotIn(digit_chars);
  CHECK_EQ(result->position().line, original_position.line);
  CHECK_GT(result->position(), original_position);

  result->PushAndPop(result->position().column - original_position.column,
                     number_modifiers, properties);
}
}  // namespace afc::editor::parsers
