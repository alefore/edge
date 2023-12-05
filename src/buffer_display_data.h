#ifndef __AFC_EDITOR_BUFFER_DISPLAY_DATA_H__
#define __AFC_EDITOR_BUFFER_DISPLAY_DATA_H__

#include "src/language/observers.h"
#include "src/language/text/line_column.h"

namespace afc::editor {
// Holds state related to a viewer (terminal) of a buffer.
class BufferDisplayData {
 public:
  language::ObservableValue<language::text::LineColumnDelta>& view_size();
  const language::ObservableValue<language::text::LineColumnDelta>& view_size()
      const;

  // See max_display_width_.
  void AddDisplayWidth(language::lazy_string::ColumnNumberDelta display_width);
  language::lazy_string::ColumnNumberDelta max_display_width() const;

  // See min_vertical_prefix_size_.
  void AddVerticalPrefixSize(
      language::text::LineNumberDelta vertical_prefix_size);
  std::optional<language::text::LineNumberDelta> min_vertical_prefix_size()
      const;

  language::text::LineNumberDelta content_lines() const;
  void set_content_lines(language::text::LineNumberDelta);

 private:
  // We remember the size that this buffer had when we last drew it.
  //
  // If the buffer changes size, we'll aim to full all screen space; otherwise,
  // we'll aim to avoid flickering. That means that scrolling in the buffer
  // (without changing it) will always aim to avoid flickering; modifying the
  // buffer will only trigger flickering if the size changes.
  language::text::LineNumberDelta content_lines_ =
      language::text::LineNumberDelta();

  language::ObservableValue<language::text::LineColumnDelta> view_size_;

  // The maximum width that has been found for a screen line corresponding to
  // this buffer, since the OpenBuffer instance was created. Includes all the
  // metadata for the line (numbers, syntax tree, scroll bar, marks metadata,
  // etc.).
  //
  // This is used when centering the output of a buffer horizontally, to prevent
  // jittering.
  language::lazy_string::ColumnNumberDelta max_display_width_ =
      language::lazy_string::ColumnNumberDelta(0);

  // The smallest vertical prefix we've used while showing this buffer. A
  // vertical prefix is a block of empty lines.
  //
  // This is used when centering the output of a buffer vertically, to prevent
  // jittering.
  std::optional<language::text::LineNumberDelta> min_vertical_prefix_size_ =
      std::nullopt;
};
}  // namespace afc::editor
#endif  // __AFC_EDITOR_BUFFER_DISPLAY_DATA_H__
