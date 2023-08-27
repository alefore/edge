#ifndef __AFC_EDITOR_WIDGET_H__
#define __AFC_EDITOR_WIDGET_H__

#include <list>
#include <memory>

#include "src/buffer.h"
#include "src/language/text/line_column.h"
#include "src/line_with_cursor.h"
#include "src/vm/public/environment.h"

namespace afc::editor {
class Widget {
 public:
  ~Widget() = default;

  struct OutputProducerOptions {
    language::text::LineColumnDelta size;
    enum class MainCursorDisplay {
      kActive,
      // The main cursor should be shown as inactive.
      kInactive
    };
    MainCursorDisplay main_cursor_display = MainCursorDisplay::kActive;
  };
  virtual LineWithCursor::Generator::Vector CreateOutput(
      OutputProducerOptions options) const = 0;

  virtual language::text::LineNumberDelta MinimumLines() const = 0;
  virtual language::text::LineNumberDelta DesiredLines() const = 0;
};
}  // namespace afc::editor

#endif  // __AFC_EDITOR_WIDGET_H__
