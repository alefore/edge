#ifndef __AFC_EDITOR_BUFFER_OUTPUT_PRODUCER_H__
#define __AFC_EDITOR_BUFFER_OUTPUT_PRODUCER_H__

#include "src/buffer.h"
#include "src/output_producer.h"

namespace afc {
namespace editor {

class BufferOutputProducer : public OutputProducer {
 public:
  BufferOutputProducer(std::shared_ptr<OpenBuffer> buffer, size_t lines_shown,
                       size_t columns_shown, LineColumn view_start,
                       std::shared_ptr<const ParseTree> zoomed_out_tree);

  void WriteLine(Options options) override;

  // Returns the prediction for the range (from the buffer) that will be
  // displayed in the next line. The start is known to be accurate, but the end
  // could be inaccurate because we don't fully know how much certain characters
  // (mostly tabs, but also multi-width characters) will actually consume.
  Range GetCurrentRange() const;
  std::set<size_t> GetCurrentCursors() const;

  bool HasActiveCursor() const;

 private:
  Range GetRangeStartingAt(LineColumn start) const;

  const std::shared_ptr<OpenBuffer> buffer_;
  const size_t lines_shown_;
  const size_t columns_shown_;
  const LineColumn view_start_;

  // Key is line number.
  const std::map<size_t, std::set<size_t>> cursors_;
  const std::shared_ptr<const ParseTree> root_;
  const ParseTree* current_tree_;
  const std::shared_ptr<const ParseTree> zoomed_out_tree_;

  // The range in the input (in buffer_) for the next line to output.
  Range range_;

  // The last buffer line that was printed. Used for the number prefix.
  size_t last_line_ = std::numeric_limits<size_t>::max();

  // When we're outputing information about other buffers (mostly useful just
  // for the list of open buffers), keeps track of those we've already shown, to
  // only output their flags in their first line.
  std::unordered_set<const OpenBuffer*> buffers_shown_;
};

}  // namespace editor
}  // namespace afc

#endif  // __AFC_EDITOR_BUFFER_OUTPUT_PRODUCER_H__
