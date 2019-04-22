#ifndef __AFC_EDITOR_BUFFER_TREE_HORIZONTAL_H__
#define __AFC_EDITOR_BUFFER_TREE_HORIZONTAL_H__

#include <list>
#include <memory>

#include "src/buffer_tree.h"
#include "src/output_producer.h"
#include "src/parse_tree.h"
#include "src/tree.h"
#include "src/vm/public/environment.h"

namespace afc {
namespace editor {

class BufferTreeHorizontal : public BufferTree {
 private:
  struct ConstructorAccessTag {};

 public:
  static std::unique_ptr<BufferTreeHorizontal> New(
      std::vector<std::unique_ptr<BufferTree>> children, size_t active);

  BufferTreeHorizontal(ConstructorAccessTag,
                       std::vector<std::unique_ptr<BufferTree>> children,
                       size_t active);

  static std::unique_ptr<BufferTree> AddHorizontalSplit(
      std::unique_ptr<BufferTree> tree);

  // `tree` may be of any type (not only BufferTreeHorizontal).
  static std::unique_ptr<BufferTree> RemoveActiveLeaf(
      std::unique_ptr<BufferTree> tree);

  std::shared_ptr<OpenBuffer> LockActiveLeaf() const override;

  void SetActiveLeafBuffer(std::shared_ptr<OpenBuffer> buffer) override;
  void SetActiveLeaf(size_t position) override;
  void AdvanceActiveLeaf(int delta) override;

  size_t CountLeafs() const override;

  wstring Name() const override;
  wstring ToString() const override;

  std::unique_ptr<OutputProducer> CreateOutputProducer() override;

  void SetLines(size_t lines) override;
  size_t MinimumLines() override;

  void PushChildren(std::unique_ptr<BufferTree> children);
  size_t children_count() const;

 private:
  void RecomputeLinesPerChild();

  // Doesn't wrap. Returns the number of steps pending.
  int AdvanceActiveLeafWithoutWrapping(int delta);
  static std::unique_ptr<BufferTree> RemoveActiveLeafInternal(
      std::unique_ptr<BufferTree> tree);

  std::vector<std::unique_ptr<BufferTree>> children_;
  size_t active_;

  size_t lines_;
  std::vector<size_t> lines_per_child_;
};

}  // namespace editor
}  // namespace afc

#endif  // __AFC_EDITOR_BUFFER_TREE_HORIZONTAL_H__
