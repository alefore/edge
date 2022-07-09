#ifndef __AFC_EDITOR_BUFFER_WIDGET_H__
#define __AFC_EDITOR_BUFFER_WIDGET_H__

#include <list>
#include <memory>

#include "src/buffer_contents_view_layout.h"
#include "src/buffer_display_data.h"
#include "src/language/safe_types.h"
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
};

struct BufferOutputProducerInput {
  Widget::OutputProducerOptions output_producer_options;
  const OpenBuffer& buffer;

  // This is an input/output parameter: the viewer should update the state here.
  BufferDisplayData& buffer_display_data;

  LineColumn view_start;
  enum class StatusBehavior { kShow, kIgnore };
  StatusBehavior status_behavior = StatusBehavior::kShow;
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

#endif  // __AFC_EDITOR_BUFFER_WIDGET_H__
