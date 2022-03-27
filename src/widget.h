#ifndef __AFC_EDITOR_WIDGET_H__
#define __AFC_EDITOR_WIDGET_H__

#include <list>
#include <memory>

#include "src/buffer.h"
#include "src/lazy_string.h"
#include "src/output_producer.h"
#include "src/vm/public/environment.h"

namespace afc::editor {
class Widget {
 public:
  ~Widget() = default;

  struct OutputProducerOptions {
    LineColumnDelta size;
    enum class MainCursorBehavior { kHighlight, kIgnore };
    MainCursorBehavior main_cursor_behavior;
  };
  virtual LineWithCursor::Generator::Vector CreateOutput(
      OutputProducerOptions options) const = 0;

  virtual LineNumberDelta MinimumLines() const = 0;
  virtual LineNumberDelta DesiredLines() const = 0;
};
}  // namespace afc::editor

#endif  // __AFC_EDITOR_WIDGET_H__
