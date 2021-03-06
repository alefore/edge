#ifndef __AFC_EDITOR_WIDGET_H__
#define __AFC_EDITOR_WIDGET_H__

#include <list>
#include <memory>

#include "src/buffer.h"
#include "src/lazy_string.h"
#include "src/output_producer.h"
#include "src/vm/public/environment.h"

namespace afc {
namespace editor {

class BufferWidget;

class Widget {
 public:
  ~Widget() = default;

  struct OutputProducerOptions {
    LineColumnDelta size;
    enum class MainCursorBehavior { kHighlight, kIgnore };
    MainCursorBehavior main_cursor_behavior;

    std::optional<size_t> position_in_parent = {};
    bool is_active = true;
  };
  virtual std::unique_ptr<OutputProducer> CreateOutputProducer(
      OutputProducerOptions options) const = 0;

  virtual LineNumberDelta MinimumLines() const = 0;
};

// std::ostream& operator<<(std::ostream& os, const Widget& lc);

}  // namespace editor
}  // namespace afc

#endif  // __AFC_EDITOR_WIDGET_H__
