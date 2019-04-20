#ifndef __AFC_EDITOR_OUTPUT_PRODUCER_H__
#define __AFC_EDITOR_OUTPUT_PRODUCER_H__

#include <vector>

#include "src/output_receiver.h"

namespace afc {
namespace editor {

class OutputProducer {
 public:
  struct Options {
    std::vector<std::unique_ptr<OutputReceiver>> lines;

    // Output parameter. If the active cursor is found, stores it here. For
    // example, if it was drawn in the OutputReceiver of lines[2], at the
    // column 10, this will be set to LineColumn(2, 10). May be nullptr.
    std::optional<LineColumn>* active_cursor = nullptr;
  };
  virtual void Produce(Options options) = 0;
};

}  // namespace editor
}  // namespace afc

#endif  // __AFC_EDITOR_OUTPUT_PRODUCER_H__
