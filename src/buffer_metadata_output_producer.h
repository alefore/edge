#ifndef __AFC_EDITOR_BUFFER_METADATA_OUTPUT_PRODUCER_H__
#define __AFC_EDITOR_BUFFER_METADATA_OUTPUT_PRODUCER_H__

#include "src/buffer.h"
#include "src/line_scroll_control.h"
#include "src/output_producer.h"

namespace afc {
namespace editor {

class MetadataLine;
// OutputProducer that prints the metadata that is usually shown right after the
// contents of the buffer (at the right side).
class BufferMetadataOutputProducer : public OutputProducer {
 public:
  BufferMetadataOutputProducer(
      std::shared_ptr<OpenBuffer> buffer,
      std::vector<BufferContentsWindow::Line> screen_lines,
      LineNumberDelta lines_shown,
      std::shared_ptr<const ParseTree> zoomed_out_tree);

  LineWithCursor::Generator::Vector Produce(LineNumberDelta lines);

 private:
  LineNumber initial_line() const;
  std::list<MetadataLine> Prepare(Range range, bool has_previous);
  Line GetDefaultInformation(LineNumber line);
  MetadataLine NewMetadataLine(wchar_t info_char, LineModifier modifier,
                               Line suffix);

  Line ComputeMarksSuffix(LineNumber line);
  Line ComputeCursorsSuffix(LineNumber line);
  Line ComputeScrollBarSuffix(LineNumber line);

  const std::shared_ptr<OpenBuffer> buffer_;
  const std::vector<BufferContentsWindow::Line> screen_lines_;
  const LineNumberDelta lines_shown_;

  // Key is line number.
  const std::shared_ptr<const ParseTree> root_;
  const ParseTree* current_tree_;
  const std::shared_ptr<const ParseTree> zoomed_out_tree_;
};

}  // namespace editor
}  // namespace afc

#endif  // __AFC_EDITOR_BUFFER_METADATA_OUTPUT_PRODUCER_H__
