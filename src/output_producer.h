#ifndef __AFC_EDITOR_OUTPUT_PRODUCER_H__
#define __AFC_EDITOR_OUTPUT_PRODUCER_H__

#include <vector>

#include "src/output_receiver.h"

namespace afc {
namespace editor {

// Can be used to render a view of something once, line by line.
class OutputProducer {
 public:
  struct Options {
    std::unique_ptr<OutputReceiver> receiver;

    // Output parameter. If the active cursor is found in the line, stores here
    // the column in which it was output here. May be nullptr.
    std::optional<ColumnNumber>* active_cursor = nullptr;
  };
  virtual void WriteLine(Options options) = 0;
};

}  // namespace editor
}  // namespace afc

#endif  // __AFC_EDITOR_OUTPUT_PRODUCER_H__
