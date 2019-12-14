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
  // Typically a copy of `view_start`, but may have been adjusted.
  LineColumn view_start;
};

struct BufferOutputProducerInput {
  Widget::OutputProducerOptions output_producer_options;
  std::shared_ptr<OpenBuffer> buffer;
  LineColumn view_start;
};

BufferOutputProducerOutput CreateBufferOutputProducer(
    BufferOutputProducerInput input);

class BufferWidget : public Widget {
 private:
  struct ConstructorAccessTag {};

 public:
  static std::unique_ptr<BufferWidget> New(std::weak_ptr<OpenBuffer> buffer);
  static std::unique_ptr<BufferWidget> New() {
    return New(std::shared_ptr<OpenBuffer>());
  }

  BufferWidget(ConstructorAccessTag, std::weak_ptr<OpenBuffer> buffer);

  // Overrides from Widget.
  wstring Name() const override;
  wstring ToString() const override;

  BufferWidget* GetActiveLeaf() override;
  const BufferWidget* GetActiveLeaf() const override;
  void ForEachBufferWidget(
      std::function<void(BufferWidget*)> callback) override;
  void ForEachBufferWidgetConst(
      std::function<void(const BufferWidget*)> callback) const override;

  std::unique_ptr<OutputProducer> CreateOutputProducer(
      OutputProducerOptions options) const override;

  LineNumberDelta MinimumLines() const override;

  void RemoveBuffer(OpenBuffer* buffer) override;

  size_t CountLeaves() const override;
  int AdvanceActiveLeafWithoutWrapping(int delta) override;
  void SetActiveLeavesAtStart() override;

  LineColumn view_start() const;

  // Custom methods.
  std::shared_ptr<OpenBuffer> Lock() const;
  void SetBuffer(std::weak_ptr<OpenBuffer> buffer);

 private:
  std::weak_ptr<OpenBuffer> leaf_;

  // The position in the buffer where the view begins.
  // TODO: Find a better way than making this mutable.
  mutable LineColumn view_start_;
};

}  // namespace afc::editor

#endif  // __AFC_EDITOR_BUFFER_LEAF_H__
