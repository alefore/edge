#ifndef __AFC_EDITOR_BUFFER_LEAF_H__
#define __AFC_EDITOR_BUFFER_LEAF_H__

#include <list>
#include <memory>

#include "src/buffer_tree.h"
#include "src/output_producer.h"
#include "src/parse_tree.h"
#include "src/tree.h"

namespace afc {
namespace editor {

class BufferTreeLeaf : public BufferTree {
 private:
  struct ConstructorAccessTag {};

 public:
  static std::unique_ptr<BufferTreeLeaf> New(std::weak_ptr<OpenBuffer> buffer);

  BufferTreeLeaf(ConstructorAccessTag, std::weak_ptr<OpenBuffer> buffer);

  BufferTreeLeaf* GetActiveLeaf() override;

  void SetActiveLeafBuffer(std::shared_ptr<OpenBuffer> buffer) override;
  void SetActiveLeaf(size_t position) override;
  void AdvanceActiveLeaf(int delta) override;

  size_t CountLeafs() const override;

  wstring Name() const override;
  wstring ToString() const override;

  std::unique_ptr<OutputProducer> CreateOutputProducer() override;

  void SetLines(size_t) override;
  size_t lines() const override;
  size_t MinimumLines() override;
  LineColumn view_start() const;

  // Custom methods.
  std::shared_ptr<OpenBuffer> Lock() const;

 private:
  std::weak_ptr<OpenBuffer> leaf_;
  size_t lines_ = 0;

  // The position in the buffer where the view begins.
  LineColumn view_start_;
};

}  // namespace editor
}  // namespace afc

#endif  // __AFC_EDITOR_BUFFER_LEAF_H__
