#ifndef __AFC_EDITOR_SECTION_BRACKETS_PRODUCER_H__
#define __AFC_EDITOR_SECTION_BRACKETS_PRODUCER_H__

#include "src/line_column.h"
#include "src/line_with_cursor.h"

namespace afc::editor {
enum class SectionBracketsSide { kLeft, kRight };
LineWithCursor::Generator::Vector SectionBrackets(
    LineNumberDelta lines, SectionBracketsSide section_brackets_side);
}  // namespace afc::editor

#endif  // __AFC_EDITOR_SECTION_BRACKETS_PRODUCER_H__
