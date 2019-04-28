#ifndef __AFC_EDITOR_LINE_NUMBER_OUTPUT_PRODUCER_H__
#define __AFC_EDITOR_LINE_NUMBER_OUTPUT_PRODUCER_H__

#include <list>
#include <memory>

#include "src/line_scroll_control.h"
#include "src/output_producer.h"
#include "src/parse_tree.h"
#include "src/tree.h"
#include "src/widget.h"

namespace afc {
namespace editor {

class LineNumberOutputProducer : public OutputProducer {
 public:
  static size_t PrefixWidth(size_t lines_size);

  LineNumberOutputProducer(
      std::shared_ptr<OpenBuffer> buffer,
      std::unique_ptr<LineScrollControl::Reader> line_scroll_control_reader);

  void WriteLine(Options options) override;

  size_t width() const;

 private:
  const size_t width_;
  const std::shared_ptr<OpenBuffer> buffer_;
  const std::unique_ptr<LineScrollControl::Reader> line_scroll_control_reader_;
  std::optional<size_t> last_line_;
};

}  // namespace editor
}  // namespace afc

#endif  // __AFC_EDITOR_LINE_NUMBER_OUTPUT_PRODUCER_H__
