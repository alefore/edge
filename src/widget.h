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

  virtual wstring Name() const = 0;
  virtual wstring ToString() const = 0;

  virtual BufferWidget* GetActiveLeaf() = 0;
  virtual const BufferWidget* GetActiveLeaf() const = 0;

  struct OutputProducerOptions {
    LineColumnDelta size;
    enum class MainCursorBehavior { kHighlight, kIgnore };
    MainCursorBehavior main_cursor_behavior;
  };
  virtual std::unique_ptr<OutputProducer> CreateOutputProducer(
      OutputProducerOptions options) const = 0;

  virtual LineNumberDelta MinimumLines() const = 0;
};

std::ostream& operator<<(std::ostream& os, const Widget& lc);

}  // namespace editor
}  // namespace afc

#endif  // __AFC_EDITOR_WIDGET_H__
