#ifndef __AFC_EDITOR_BUFFER_OUTPUT_PRODUCER_H__
#define __AFC_EDITOR_BUFFER_OUTPUT_PRODUCER_H__

#include "src/buffer.h"
#include "src/line_column.h"
#include "src/line_scroll_control.h"
#include "src/output_producer.h"

namespace afc {
namespace editor {

// Unlike `CreateBufferOutputProducer`, doesn't do much beyond just displaying
// the contents of the buffer (with syntax highlighting).
LineWithCursor::Generator::Vector ProduceBufferView(
    std::shared_ptr<OpenBuffer> buffer,
    std::list<BufferContentsWindow::Line> lines,
    Widget::OutputProducerOptions output_producer_options);

}  // namespace editor
}  // namespace afc

#endif  // __AFC_EDITOR_BUFFER_OUTPUT_PRODUCER_H__
