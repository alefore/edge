#ifndef __AFC_EDITOR_WIDGET_H__
#define __AFC_EDITOR_WIDGET_H__

#include <list>
#include <memory>

#include "src/buffer.h"
#include "src/lazy_string.h"
#include "src/line_with_cursor.h"
#include "src/vm/public/environment.h"

namespace afc::editor {
class Widget {
 public:
  ~Widget() = default;

  struct OutputProducerOptions {
    LineColumnDelta size;
    enum class MainCursorDisplay {
      kActive,
      // The main cursor should be shown as inactive.
      kInactive
    };
    MainCursorDisplay main_cursor_display = MainCursorDisplay::kActive;
  };
  virtual LineWithCursor::Generator::Vector CreateOutput(
      OutputProducerOptions options) const = 0;

  virtual LineNumberDelta MinimumLines() const = 0;
  virtual LineNumberDelta DesiredLines() const = 0;
};
}  // namespace afc::editor

#endif  // __AFC_EDITOR_WIDGET_H__
