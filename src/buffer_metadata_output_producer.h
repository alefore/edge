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
      size_t lines_shown, size_t initial_line,
      std::shared_ptr<const ParseTree> zoomed_out_tree);

  void WriteLine(Options options) override;

 private:
  void Prepare(size_t line);
  wstring GetDefaultInformation(size_t line);

  const std::shared_ptr<OpenBuffer> buffer_;
  const std::unique_ptr<LineScrollControl::Reader> line_scroll_control_reader_;
  const size_t lines_shown_;
  const size_t initial_line_;

  // Key is line number.
  const std::shared_ptr<const ParseTree> root_;
  const ParseTree* current_tree_;
  const std::shared_ptr<const ParseTree> zoomed_out_tree_;

  // When we're outputing information about other buffers (mostly useful just
  // for the list of open buffers), keeps track of those we've already shown, to
  // only output their flags in their first line.
  std::unordered_set<const OpenBuffer*> buffers_shown_;

  // Fields with output to display for each line in the buffer. They are set by
  // `Prepare`.
  struct LineData {
    wchar_t info_char_;
    LineModifier info_char_modifier_;

    std::optional<wstring> additional_information_;
    std::list<LineMarks::Mark> marks_;
    std::list<LineMarks::Mark> marks_expired_;
  };

  std::optional<LineData> line_data_;
};

}  // namespace editor
}  // namespace afc

#endif  // __AFC_EDITOR_BUFFER_METADATA_OUTPUT_PRODUCER_H__
