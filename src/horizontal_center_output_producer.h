#ifndef __AFC_EDITOR_HORIZONTAL_CENTER_OUTPUT_PRODUCER_H__
#define __AFC_EDITOR_HORIZONTAL_CENTER_OUTPUT_PRODUCER_H__

#include <memory>
#include <vector>

#include "src/line_column.h"
#include "src/line_with_cursor.h"

namespace afc::editor {
LineWithCursor::Generator::Vector CenterOutput(
    LineWithCursor::Generator::Vector lines,
    language::lazy_string::ColumnNumberDelta width);
}  // namespace afc::editor

#endif  // __AFC_EDITOR_HORIZONTAL_CENTER_OUTPUT_PRODUCER_H__
