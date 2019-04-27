#ifndef __AFC_EDITOR_BUFFERS_LIST_H__
#define __AFC_EDITOR_BUFFERS_LIST_H__

#include <list>
#include <memory>

#include "src/output_producer.h"
#include "src/parse_tree.h"
#include "src/tree.h"
#include "src/widget.h"

namespace afc {
namespace editor {

class BuffersListProducer : public OutputProducer {
 public:
  struct Entry {
    std::shared_ptr<OpenBuffer> buffer;
    size_t index;
  };

  static const size_t kMinimumColumnsPerBuffer = 20;

  BuffersListProducer(std::vector<std::vector<Entry>> buffers,
                      size_t active_index);

  void WriteLine(Options options) override;

 private:
  const std::vector<std::vector<Entry>> buffers_;
  const size_t active_index_;
  const size_t max_index_;
  const size_t prefix_width_;
  int current_line_ = 0;
};

}  // namespace editor
}  // namespace afc

#endif  // __AFC_EDITOR_BUFFERS_LIST_H__
