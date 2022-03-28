#include "src/line_number_output_producer.h"

#include <glog/logging.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <iostream>

#include "src/buffer.h"
#include "src/buffer_output_producer.h"
#include "src/buffer_variables.h"
#include "src/buffer_widget.h"
#include "src/char_buffer.h"
#include "src/columns_vector.h"
#include "src/editor.h"
#include "src/editor_variables.h"
#include "src/lazy_string.h"
#include "src/lazy_string_append.h"
#include "src/line_scroll_control.h"
#include "src/widget.h"
#include "src/wstring.h"

namespace afc {
namespace editor {

/* static */ ColumnNumberDelta LineNumberOutputWidth(
    LineNumberDelta lines_size) {
  return ColumnNumberDelta(
      1 +
      (LineNumber() + lines_size - LineNumberDelta(1)).ToUserString().size());
}

LineWithCursor::Generator::Vector LineNumberOutput(
    const OpenBuffer& buffer,
    const std::vector<BufferContentsWindow::Line>& screen_lines) {
  ColumnNumberDelta width = max(LineNumberOutputWidth(buffer.lines_size()),
                                ColumnNumberDelta(buffer.editor().Read(
                                    editor_variables::numbers_column_padding)));
  LineWithCursor::Generator::Vector output{.lines = {}, .width = width};
  for (const BufferContentsWindow::Line& screen_line : screen_lines) {
    if (screen_line.range.begin.line > buffer.EndLine()) {
      return output;  // The buffer is smaller than the screen.
    }

    HashableContainer<LineModifierSet> modifiers;
    if (screen_line.current_cursors.empty()) {
      modifiers.container = {LineModifier::DIM};
    } else if (screen_line.has_active_cursor ||
               buffer.Read(buffer_variables::multiple_cursors)) {
      modifiers.container = {LineModifier::CYAN, LineModifier::BOLD};
    } else {
      modifiers.container = {LineModifier::BLUE};
    }

    output.lines.push_back(LineWithCursor::Generator::New(CaptureAndHash(
        [](Range range, ColumnNumberDelta width,
           HashableContainer<LineModifierSet> modifiers) {
          std::wstring number = range.begin.column.IsZero()
                                    ? range.begin.line.ToUserString()
                                    : L"↪";
          CHECK_LE(ColumnNumberDelta(number.size() + 1), width);
          auto padding = ColumnNumberDelta::PaddingString(
              width - ColumnNumberDelta(number.size() + 1), L' ');

          Line::Options line_options;
          line_options.AppendString(
              StringAppend(padding, NewLazyString(number + L":")),
              modifiers.container);
          return LineWithCursor(Line(std::move(line_options)));
        },
        std::move(screen_line.range), width, std::move(modifiers))));
  }
  return output;
}

}  // namespace editor
}  // namespace afc
