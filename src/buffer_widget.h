#ifndef __AFC_EDITOR_BUFFER_LEAF_H__
#define __AFC_EDITOR_BUFFER_LEAF_H__

#include <list>
#include <memory>

#include "src/language/safe_types.h"
#include "src/line_scroll_control.h"
#include "src/line_with_cursor.h"
#include "src/widget.h"

namespace afc::editor {

struct BufferOutputProducerOutput {
  LineWithCursor::Generator::Vector lines;
  // Typically a copy of `BufferOutputProducerInput::view_start`, but may have
  // been adjusted.
  //
  // It is the responsibility of the caller to propagate it to the buffer.
  LineColumn view_start;

  // The effective size of the view.
  //
  // It is the responsibility of the caller to propagate it to the buffer.
  LineColumnDelta view_size;

  // The width of the longest line shown, including all metadata but excluding
  // the "centering" padding.
  //
  // It is the responsibility of the caller to propagate it to the buffer.
  ColumnNumberDelta max_display_width;

  // The number of empty "space" lines added (to center the buffer vertically in
  // the screen).
  //
  // It is the responsibility of the caller to propagate it to the buffer.
  std::optional<LineNumberDelta> vertical_prefix_size;
};

struct BufferOutputProducerInput {
  Widget::OutputProducerOptions output_producer_options;
  const OpenBuffer& buffer;
  LineColumn view_start;
  enum class StatusBehavior { kShow, kIgnore };
  StatusBehavior status_behavior = StatusBehavior::kShow;
  std::optional<LineNumberDelta> min_vertical_prefix_size;
};

// Handles things like `multiple_cursors`, `paste_mode`, `scrollbar`, displaying
// metadata, line numbers, etc..
//
// Does not mutate the buffer in any way. It is the caller's responsibility to
// honor variable reload_on_display, as well as to apply changes communicated
// through the output.
BufferOutputProducerOutput CreateBufferOutputProducer(
    BufferOutputProducerInput input);

class BufferWidget : public Widget {
 public:
  struct Options {
    language::gc::WeakPtr<OpenBuffer> buffer;
    bool is_active = false;
    // Presence of this field indicates that a frame should be drawn around the
    // buffer.
    std::optional<size_t> position_in_parent = {};
  };

  BufferWidget(Options options);

  // Overrides from Widget.
  LineWithCursor::Generator::Vector CreateOutput(
      OutputProducerOptions options) const override;

  LineNumberDelta MinimumLines() const override;
  LineNumberDelta DesiredLines() const override;

  LineColumn view_start() const;

  // Custom methods.
  std::optional<language::gc::Root<OpenBuffer>> Lock() const;
  void SetBuffer(language::gc::WeakPtr<OpenBuffer> buffer);

 private:
  // Non-const: SetBuffer modifies it.
  Options options_;
};

}  // namespace afc::editor

#endif  // __AFC_EDITOR_BUFFER_LEAF_H__
