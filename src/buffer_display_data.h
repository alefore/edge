#ifndef __AFC_EDITOR_BUFFER_DISPLAY_DATA_H__
#define __AFC_EDITOR_BUFFER_DISPLAY_DATA_H__

#include "line_column.h"

namespace afc::editor {
// Holds state related to a viewer (terminal) of a buffer.
class BufferDisplayData {
 public:
  // See max_display_width_.
  void AddDisplayWidth(ColumnNumberDelta display_width);
  ColumnNumberDelta max_display_width() const;

  // See min_vertical_prefix_size_.
  void AddVerticalPrefixSize(LineNumberDelta vertical_prefix_size);
  std::optional<LineNumberDelta> min_vertical_prefix_size() const;

 private:
  // The maximum width that has been found for a screen line corresponding to
  // this buffer, since the OpenBuffer instance was created. Includes all the
  // metadata for the line (numbers, syntax tree, scroll bar, marks metadata,
  // etc.).
  //
  // This is used when centering the output of a buffer horizontally, to prevent
  // jittering.
  //
  // Cleared when the buffer is reloaded.
  ColumnNumberDelta max_display_width_ = ColumnNumberDelta(0);

  // The smallest vertical prefix we've used while showing this buffer. A
  // vertical prefix is a block of empty lines.
  //
  // This is used when centering the output of a buffer vertically, to prevent
  // jittering.
  //
  // Cleared when the buffer is reloaded.
  std::optional<LineNumberDelta> min_vertical_prefix_size_ = std::nullopt;
};
}  // namespace afc::editor
#endif  // __AFC_EDITOR_BUFFER_DISPLAY_DATA_H__
