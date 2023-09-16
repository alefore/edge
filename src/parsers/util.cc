#include "src/parsers/util.h"

namespace afc::editor::parsers {
using infrastructure::screen::LineModifier;
using infrastructure::screen::LineModifierSet;
using language::lazy_string::ColumnNumber;
using language::lazy_string::ColumnNumberDelta;
using language::text::LineColumn;

static const LineModifierSet BAD_PARSE_MODIFIERS =
    LineModifierSet({LineModifier::kBgRed, LineModifier::kBold});

void ParseDoubleQuotedString(ParseData* result,
                             LineModifierSet string_modifiers) {
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
                       string_modifiers);
    // TODO: words_parser_->FindChildren(result->buffer(), tree);
  } else {
    result->set_position(original_position);
    result->PushAndPop(ColumnNumberDelta(1), BAD_PARSE_MODIFIERS);
  }
}
}  // namespace afc::editor::parsers
