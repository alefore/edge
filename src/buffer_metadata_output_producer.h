#ifndef __AFC_EDITOR_BUFFER_METADATA_OUTPUT_PRODUCER_H__
#define __AFC_EDITOR_BUFFER_METADATA_OUTPUT_PRODUCER_H__

#include "src/buffer.h"
#include "src/line_scroll_control.h"
#include "src/output_producer.h"

namespace afc {
namespace editor {

class BufferMetadataOutputProducer : public OutputProducer {
 public:
  BufferMetadataOutputProducer(
      std::shared_ptr<OpenBuffer> buffer,
      std::unique_ptr<LineScrollControl::Reader> line_scroll_control_reader,
      LineNumberDelta lines_shown,
      std::shared_ptr<const ParseTree> zoomed_out_tree);

  Generator Next() override;

 private:
  void Prepare(Range range);
  wstring GetDefaultInformation(LineNumber line);
  void PushGenerator(wchar_t info_char, LineModifier modifier, wstring str);

  wchar_t ComputeScrollBarCharacter(LineNumber line);

  const std::shared_ptr<OpenBuffer> buffer_;
  const std::unique_ptr<LineScrollControl::Reader> line_scroll_control_reader_;
  const LineNumberDelta lines_shown_;

  // Key is line number.
  const std::shared_ptr<const ParseTree> root_;
  const ParseTree* current_tree_;
  const std::shared_ptr<const ParseTree> zoomed_out_tree_;

  // Set the first time we get a range from `line_scroll_control_reader_`.
  std::optional<LineNumber> initial_line_;

  // When we're outputing information about other buffers (mostly useful just
  // for the list of open buffers), keeps track of those we've already shown, to
  // only output their flags in their first line.
  std::unordered_set<const OpenBuffer*> buffers_shown_;

  std::list<Generator> range_data_;
};

}  // namespace editor
}  // namespace afc

#endif  // __AFC_EDITOR_BUFFER_METADATA_OUTPUT_PRODUCER_H__
