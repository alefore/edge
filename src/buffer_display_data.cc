#include "src/buffer_display_data.h"

#include "src/line_column.h"

namespace afc::editor {
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
}  // namespace afc::editor
