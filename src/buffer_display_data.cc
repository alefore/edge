#include "src/buffer_display_data.h"

#include "src/language/text/line_column.h"

namespace afc::editor {
using language::ObservableValue;
using language::lazy_string::ColumnNumberDelta;
using language::text::LineColumnDelta;
using language::text::LineNumberDelta;

ObservableValue<LineColumnDelta>& BufferDisplayData::view_size() {
  return view_size_;
}

const ObservableValue<LineColumnDelta>& BufferDisplayData::view_size() const {
  return view_size_;
}

void BufferDisplayData::AddDisplayWidth(ColumnNumberDelta display_width) {
  max_display_width_ = std::max(max_display_width_, display_width);
}

ColumnNumberDelta BufferDisplayData::max_display_width() const {
  return max_display_width_;
}

void BufferDisplayData::AddVerticalPrefixSize(
    LineNumberDelta vertical_prefix_size) {
  min_vertical_prefix_size_ =
      std::min(vertical_prefix_size,
               min_vertical_prefix_size_.value_or(vertical_prefix_size));
}

std::optional<LineNumberDelta> BufferDisplayData::min_vertical_prefix_size()
    const {
  return min_vertical_prefix_size_;
}

language::text::LineNumberDelta BufferDisplayData::content_lines() const {
  return content_lines_;
}

void BufferDisplayData::set_content_lines(
    language::text::LineNumberDelta input) {
  content_lines_ = input;
}
}  // namespace afc::editor
