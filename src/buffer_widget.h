#ifndef __AFC_EDITOR_BUFFER_LEAF_H__
#define __AFC_EDITOR_BUFFER_LEAF_H__

#include <list>
#include <memory>

#include "src/line_scroll_control.h"
#include "src/output_producer.h"
#include "src/viewers.h"
#include "src/widget.h"

namespace afc::editor {

struct BufferOutputProducerOutput {
  std::unique_ptr<OutputProducer> producer;
  // Typically a copy of `BufferOutputProducerInput::view_start`, but may have
  // been adjusted.
  LineColumn view_start;
};

struct BufferOutputProducerInput {
  Widget::OutputProducerOptions output_producer_options;
  std::shared_ptr<OpenBuffer> buffer;
  LineColumn view_start;
};

// Handles things like `multiple_cursors`, `paste_mode`, `scrollbar`, displaying
// metadata, line numbers, etc..
BufferOutputProducerOutput CreateBufferOutputProducer(
    BufferOutputProducerInput input);

class BufferWidget : public Widget {
 public:
  struct Options {
    std::weak_ptr<OpenBuffer> buffer = std::shared_ptr<OpenBuffer>();
  };

  BufferWidget(Options options);

  // Overrides from Widget.
  std::unique_ptr<OutputProducer> CreateOutputProducer(
      OutputProducerOptions options) const override;

  LineNumberDelta MinimumLines() const override;

  LineColumn view_start() const;

  // Custom methods.
  std::shared_ptr<OpenBuffer> Lock() const;
  void SetBuffer(std::weak_ptr<OpenBuffer> buffer);

 private:
  std::weak_ptr<OpenBuffer> leaf_;
};

}  // namespace afc::editor

#endif  // __AFC_EDITOR_BUFFER_LEAF_H__
