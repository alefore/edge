#ifndef __AFC_EDITOR_LINE_NUMBER_OUTPUT_PRODUCER_H__
#define __AFC_EDITOR_LINE_NUMBER_OUTPUT_PRODUCER_H__

#include <list>
#include <memory>

#include "src/buffer.h"
#include "src/buffer_contents_view_layout.h"
#include "src/language/text/line_column.h"
#include "src/line_with_cursor.h"

namespace afc::editor {

language::lazy_string::ColumnNumberDelta LineNumberOutputWidth(
    language::text::LineNumberDelta lines_size);

LineWithCursor::Generator::Vector LineNumberOutput(
    const OpenBuffer& buffer,
    const std::vector<BufferContentsViewLayout::Line>& screen_lines);

}  // namespace afc::editor

#endif  // __AFC_EDITOR_LINE_NUMBER_OUTPUT_PRODUCER_H__
