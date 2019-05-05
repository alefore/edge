#ifndef __AFC_EDITOR_BUFFER_OUTPUT_PRODUCER_H__
#define __AFC_EDITOR_BUFFER_OUTPUT_PRODUCER_H__

#include "src/buffer.h"
#include "src/line_column.h"
#include "src/line_scroll_control.h"
#include "src/output_producer.h"

namespace afc {
namespace editor {

class BufferOutputProducer : public OutputProducer {
 public:
  BufferOutputProducer(
      std::shared_ptr<OpenBuffer> buffer,
      std::shared_ptr<LineScrollControl::Reader> line_scroll_control_reader,
      LineNumberDelta lines_shown, ColumnNumberDelta columns_shown,
      ColumnNumber initial_column,
      std::shared_ptr<const ParseTree> zoomed_out_tree);

  Generator Next() override;

 private:
  Range GetRange(LineColumn begin);

  const std::shared_ptr<OpenBuffer> buffer_;
  const std::shared_ptr<LineScrollControl::Reader> line_scroll_control_reader_;
  const LineNumberDelta lines_shown_;
  const ColumnNumberDelta columns_shown_;
  const ColumnNumber initial_column_;

  // Key is line number.
  const std::shared_ptr<const ParseTree> root_;
  const ParseTree* current_tree_;
  const std::shared_ptr<const ParseTree> zoomed_out_tree_;

  // When we're outputing information about other buffers (mostly useful just
  // for the list of open buffers), keeps track of those we've already shown, to
  // only output their flags in their first line.
  std::unordered_set<const OpenBuffer*> buffers_shown_;
};

}  // namespace editor
}  // namespace afc

#endif  // __AFC_EDITOR_BUFFER_OUTPUT_PRODUCER_H__
