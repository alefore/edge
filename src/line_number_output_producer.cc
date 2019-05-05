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
#include "src/line_scroll_control.h"
#include "src/vertical_split_output_producer.h"
#include "src/widget.h"
#include "src/wstring.h"

namespace afc {
namespace editor {

/* static */ ColumnNumberDelta LineNumberOutputProducer::PrefixWidth(
    LineNumberDelta lines_size) {
  return ColumnNumberDelta(1 +
                           (LineNumber() + lines_size).ToUserString().size());
}

LineNumberOutputProducer::LineNumberOutputProducer(
    std::shared_ptr<OpenBuffer> buffer,
    std::unique_ptr<LineScrollControl::Reader> line_scroll_control_reader)
    : width_(PrefixWidth(buffer->lines_size())),
      buffer_(std::move(buffer)),
      line_scroll_control_reader_(std::move(line_scroll_control_reader)) {}

OutputProducer::Generator LineNumberOutputProducer::Next() {
  auto range = line_scroll_control_reader_->GetRange();
  if (range.has_value() && range.value().begin.line > buffer_->EndLine()) {
    // Happens when the buffer is smaller than the screen.
    return OutputProducer::Generator::Empty();
  }

  std::shared_ptr<bool> delete_handler;
  if (range.has_value()) {
    LOG(INFO) << "XXXX creating deleter.";
    delete_handler = std::shared_ptr<bool>(
        new bool(), [reader = line_scroll_control_reader_.get()](bool* value) {
          LOG(INFO) << "XXXX range done.";
          delete value;
          reader->RangeDone();
        });
  }

  return OutputProducer::Generator{
      std::nullopt, [range, this, delete_handler]() {
        std::wstring number =
            range.has_value() && (!last_line_.has_value() ||
                                  range.value().begin.line > last_line_.value())
                ? range.value().begin.line.ToUserString()
                : L"↪";
        if (range.has_value()) {
          last_line_ = range.value().begin.line;
        }
        CHECK_LE(ColumnNumberDelta(number.size() + 1), width_);
        std::wstring padding = ColumnNumberDelta::PaddingString(
            width_ - ColumnNumberDelta(number.size() + 1), L' ');
        LineModifierSet modifiers;
        if (!range.has_value() ||
            line_scroll_control_reader_->GetCurrentCursors().empty()) {
          modifiers.insert(LineModifier::DIM);
        } else if (line_scroll_control_reader_->HasActiveCursor() ||
                   buffer_->Read(buffer_variables::multiple_cursors)) {
          modifiers.insert(LineModifier::CYAN);
          modifiers.insert(LineModifier::BOLD);
        } else {
          modifiers.insert(LineModifier::BLUE);
        }
        Line::Options line_options;
        line_options.AppendString(padding + number + L':', modifiers);
        return LineWithCursor{std::make_shared<Line>(std::move(line_options)),
                              std::nullopt};
      }};
}

ColumnNumberDelta LineNumberOutputProducer::width() const { return width_; }

}  // namespace editor
}  // namespace afc
