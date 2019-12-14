#ifndef __AFC_EDITOR_BUFFER_LEAF_H__
#define __AFC_EDITOR_BUFFER_LEAF_H__

#include <list>
#include <memory>

#include "src/line_scroll_control.h"
#include "src/output_producer.h"
#include "src/viewers.h"
#include "src/widget.h"

namespace afc {
namespace editor {

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

  std::unique_ptr<OutputProducer> CreateOutputProducer() const override;

  void SetSize(LineColumnDelta lines) override;
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
  // When either leaf_ or lines_ changes, recomputes internal data.
  void RecomputeData();

  std::weak_ptr<OpenBuffer> leaf_;
  LineColumnDelta size_;

  LineScrollControl::Options line_scroll_control_options_;

  // The position in the buffer where the view begins.
  LineColumn view_start_;

  // The last size we've registered to the viewers of `leaf_`.
  std::optional<LineColumnDelta> buffer_view_size_registration_;
};

}  // namespace editor
}  // namespace afc

#endif  // __AFC_EDITOR_BUFFER_LEAF_H__
