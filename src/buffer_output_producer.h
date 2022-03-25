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
class BufferOutputProducer : public OutputProducer {
 public:
  BufferOutputProducer(std::shared_ptr<OpenBuffer> buffer,
                       std::list<BufferContentsWindow::Line> lines,
                       Widget::OutputProducerOptions output_producer_options);

  Output Produce(LineNumberDelta lines) override;

 private:
  Range GetRange(LineColumn begin);

  const std::shared_ptr<OpenBuffer> buffer_;
  const Widget::OutputProducerOptions output_producer_options_;

  const std::shared_ptr<const ParseTree> root_;
  const ParseTree* current_tree_;
  std::vector<BufferContentsWindow::Line> lines_;
};

}  // namespace editor
}  // namespace afc

#endif  // __AFC_EDITOR_BUFFER_OUTPUT_PRODUCER_H__
