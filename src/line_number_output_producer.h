#ifndef __AFC_EDITOR_LINE_NUMBER_OUTPUT_PRODUCER_H__
#define __AFC_EDITOR_LINE_NUMBER_OUTPUT_PRODUCER_H__

#include <list>
#include <memory>

#include "src/line_column.h"
#include "src/line_scroll_control.h"
#include "src/output_producer.h"
#include "src/widget.h"

namespace afc::editor {

ColumnNumberDelta LineNumberOutputWidth(LineNumberDelta lines_size);

LineWithCursor::Generator::Vector LineNumberOutput(
    const OpenBuffer& buffer,
    const std::vector<BufferContentsWindow::Line>& screen_lines);

}  // namespace afc::editor

#endif  // __AFC_EDITOR_LINE_NUMBER_OUTPUT_PRODUCER_H__
