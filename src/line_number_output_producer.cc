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

  OutputProducer::Generator output;

  output.inputs_hash = std::hash<std::optional<Range>>()(range) +
                       std::hash<std::optional<LineNumber>>()(last_line_) +
                       std::hash<ColumnNumberDelta>()(width_);

  LineModifierSet modifiers;
  if (!range.has_value() ||
      line_scroll_control_reader_->GetCurrentCursors().empty()) {
    modifiers.insert(LineModifier::DIM);
    output.inputs_hash.value() += std::hash<size_t>()(0);
  } else if (line_scroll_control_reader_->HasActiveCursor() ||
             buffer_->Read(buffer_variables::multiple_cursors)) {
    modifiers.insert(LineModifier::CYAN);
    modifiers.insert(LineModifier::BOLD);
    output.inputs_hash.value() += std::hash<size_t>()(1);
  } else {
    modifiers.insert(LineModifier::BLUE);
    output.inputs_hash.value() += std::hash<size_t>()(2);
  }

  output.generate = [range, last_line = last_line_, width = width_,
                     modifiers = std::move(modifiers)]() {
    std::wstring number =
        range.has_value() && (!last_line.has_value() ||
                              range.value().begin.line > last_line.value())
            ? range.value().begin.line.ToUserString()
            : L"↪";
    CHECK_LE(ColumnNumberDelta(number.size() + 1), width);
    auto padding = ColumnNumberDelta::PaddingString(
        width - ColumnNumberDelta(number.size() + 1), L' ');

    Line::Options line_options;
    line_options.AppendString(
        StringAppend(padding, NewLazyString(number + L":")), modifiers);
    return LineWithCursor{std::make_shared<Line>(std::move(line_options)),
                          std::nullopt};
  };

  if (range.has_value()) {
    last_line_ = range.value().begin.line;
    line_scroll_control_reader_->RangeDone();
  }
  return output;
}

ColumnNumberDelta LineNumberOutputProducer::width() const { return width_; }

}  // namespace editor
}  // namespace afc
