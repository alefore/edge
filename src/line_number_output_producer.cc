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
#include "src/editor.h"
#include "src/editor_variables.h"
#include "src/lazy_string.h"
#include "src/lazy_string_append.h"
#include "src/line_scroll_control.h"
#include "src/vertical_split_output_producer.h"
#include "src/widget.h"
#include "src/wstring.h"

namespace afc {
namespace editor {

/* static */ ColumnNumberDelta LineNumberOutputProducer::PrefixWidth(
    LineNumberDelta lines_size) {
  return ColumnNumberDelta(
      1 +
      (LineNumber() + lines_size - LineNumberDelta(1)).ToUserString().size());
}

LineNumberOutputProducer::LineNumberOutputProducer(
    std::shared_ptr<OpenBuffer> buffer,
    std::list<BufferContentsWindow::Line> screen_lines)
    : width_(max(PrefixWidth(buffer->lines_size()),
                 ColumnNumberDelta(buffer->editor().Read(
                     editor_variables::numbers_column_padding)))),
      buffer_(std::move(buffer)),
      screen_lines_(std::move(screen_lines)) {}

OutputProducer::Generator LineNumberOutputProducer::Next() {
  if (screen_lines_.empty()) return OutputProducer::Generator::Empty();
  BufferContentsWindow::Line screen_line = screen_lines_.front();
  screen_lines_.pop_front();
  if (screen_line.range.begin.line > buffer_->EndLine()) {
    // Happens when the buffer is smaller than the screen.
    return OutputProducer::Generator::Empty();
  }

  HashableContainer<LineModifierSet> modifiers;
  if (screen_line.current_cursors.empty()) {
    modifiers.container = {LineModifier::DIM};
  } else if (screen_line.has_active_cursor ||
             buffer_->Read(buffer_variables::multiple_cursors)) {
    modifiers.container = {LineModifier::CYAN, LineModifier::BOLD};
  } else {
    modifiers.container = {LineModifier::BLUE};
  }

  return OutputProducer::Generator::New(CaptureAndHash(
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
        return LineWithCursor{std::make_shared<Line>(std::move(line_options)),
                              std::nullopt};
      },
      std::move(screen_line.range), width_, std::move(modifiers)));
}

ColumnNumberDelta LineNumberOutputProducer::width() const { return width_; }

}  // namespace editor
}  // namespace afc
