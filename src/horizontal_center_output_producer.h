#ifndef __AFC_EDITOR_HORIZONTAL_CENTER_OUTPUT_PRODUCER_H__
#define __AFC_EDITOR_HORIZONTAL_CENTER_OUTPUT_PRODUCER_H__

#include <memory>
#include <vector>

#include "src/line_column.h"
#include "src/output_producer.h"

namespace afc::editor {
LineWithCursor::Generator::Vector CenterOutput(
    LineWithCursor::Generator::Vector lines, ColumnNumberDelta width);
}  // namespace afc::editor

#endif  // __AFC_EDITOR_HORIZONTAL_CENTER_OUTPUT_PRODUCER_H__
