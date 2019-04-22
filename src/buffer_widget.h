#ifndef __AFC_EDITOR_BUFFER_LEAF_H__
#define __AFC_EDITOR_BUFFER_LEAF_H__

#include <list>
#include <memory>

#include "src/output_producer.h"
#include "src/parse_tree.h"
#include "src/tree.h"
#include "src/widget.h"

namespace afc {
namespace editor {

class BufferWidget : public Widget {
 private:
  struct ConstructorAccessTag {};

 public:
  static std::unique_ptr<BufferWidget> New(std::weak_ptr<OpenBuffer> buffer);

  BufferWidget(ConstructorAccessTag, std::weak_ptr<OpenBuffer> buffer);

  wstring Name() const override;
  wstring ToString() const override;

  BufferWidget* GetActiveLeaf() override;

  std::unique_ptr<OutputProducer> CreateOutputProducer() override;

  void SetLines(size_t) override;
  size_t lines() const override;
  size_t MinimumLines() override;
  LineColumn view_start() const;

  // Custom methods.
  std::shared_ptr<OpenBuffer> Lock() const;
  void SetBuffer(std::weak_ptr<OpenBuffer> buffer);

 private:
  std::weak_ptr<OpenBuffer> leaf_;
  size_t lines_ = 0;

  // The position in the buffer where the view begins.
  LineColumn view_start_;
};

}  // namespace editor
}  // namespace afc

#endif  // __AFC_EDITOR_BUFFER_LEAF_H__
