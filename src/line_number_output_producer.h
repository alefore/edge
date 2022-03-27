#ifndef __AFC_EDITOR_LINE_NUMBER_OUTPUT_PRODUCER_H__
#define __AFC_EDITOR_LINE_NUMBER_OUTPUT_PRODUCER_H__

#include <list>
#include <memory>

#include "src/line_column.h"
#include "src/line_scroll_control.h"
#include "src/output_producer.h"
#include "src/widget.h"

namespace afc {
namespace editor {

class LineNumberOutputProducer : public OutputProducer {
 public:
  static ColumnNumberDelta PrefixWidth(LineNumberDelta lines_size);

  LineNumberOutputProducer(std::shared_ptr<OpenBuffer> buffer,
                           std::list<BufferContentsWindow::Line> screen_lines);

  LineWithCursor::Generator::Vector Produce(LineNumberDelta lines) override;

  ColumnNumberDelta width() const;

 private:
  const ColumnNumberDelta width_;
  const std::shared_ptr<OpenBuffer> buffer_;
  const std::list<BufferContentsWindow::Line> screen_lines_;
};

}  // namespace editor
}  // namespace afc

#endif  // __AFC_EDITOR_LINE_NUMBER_OUTPUT_PRODUCER_H__
