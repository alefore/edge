#ifndef __AFC_EDITOR_BUFFER_OUTPUT_PRODUCER_H__
#define __AFC_EDITOR_BUFFER_OUTPUT_PRODUCER_H__

#include "src/buffer.h"
#include "src/line_column.h"
#include "src/line_scroll_control.h"
#include "src/line_with_cursor.h"

namespace afc {
namespace editor {

// Unlike `CreateBufferOutputProducer`, doesn't do much beyond just displaying
// the contents of the buffer (with syntax highlighting).
//
// The output produced can be shorter than output_producer_options.size.line
// lines long.
LineWithCursor::Generator::Vector ProduceBufferView(
    const OpenBuffer& buffer,
    const std::vector<BufferContentsWindow::Line>& lines,
    const Widget::OutputProducerOptions& output_producer_options);

}  // namespace editor
}  // namespace afc

#endif  // __AFC_EDITOR_BUFFER_OUTPUT_PRODUCER_H__
