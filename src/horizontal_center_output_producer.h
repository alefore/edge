#ifndef __AFC_EDITOR_HORIZONTAL_CENTER_OUTPUT_PRODUCER_H__
#define __AFC_EDITOR_HORIZONTAL_CENTER_OUTPUT_PRODUCER_H__

#include <memory>
#include <vector>

#include "src/line_column.h"
#include "src/line_with_cursor.h"

namespace afc::editor {
// padding_lines can be shorter than lines (or empty), in which case it will be
// extended to match.
LineWithCursor::Generator::Vector CenterOutput(
    LineWithCursor::Generator::Vector lines,
    language::lazy_string::ColumnNumberDelta width,
    std::vector<LineModifier> padding_modifiers);
}  // namespace afc::editor

#endif  // __AFC_EDITOR_HORIZONTAL_CENTER_OUTPUT_PRODUCER_H__
