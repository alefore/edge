#ifndef __AFC_EDITOR_BUFFER_TREE_HORIZONTAL_H__
#define __AFC_EDITOR_BUFFER_TREE_HORIZONTAL_H__

#include <list>
#include <memory>

#include "src/output_producer.h"
#include "src/parse_tree.h"
#include "src/tree.h"
#include "src/vm/public/environment.h"
#include "src/widget.h"

namespace afc {
namespace editor {

class BufferTreeHorizontal : public Widget {
 private:
  struct ConstructorAccessTag {};

 public:
  static std::unique_ptr<BufferTreeHorizontal> New(
      std::vector<std::unique_ptr<Widget>> children, size_t active);

  static std::unique_ptr<BufferTreeHorizontal> New(
      std::unique_ptr<Widget> children);

  BufferTreeHorizontal(ConstructorAccessTag,
                       std::vector<std::unique_ptr<Widget>> children,
                       size_t active);

  wstring Name() const;
  wstring ToString() const override;

  BufferWidget* GetActiveLeaf() override;

  std::unique_ptr<OutputProducer> CreateOutputProducer() override;

  void SetLines(size_t lines) override;
  size_t lines() const override;
  size_t MinimumLines() override;

  void SetActiveLeaf(size_t position);
  void AdvanceActiveLeaf(int delta);

  size_t CountLeafs() const;

  void PushChildren(std::unique_ptr<Widget> children);
  size_t children_count() const;

  void RemoveActiveLeaf();
  void AddSplit();
  void ZoomToActiveLeaf();

 private:
  void RecomputeLinesPerChild();

  // Doesn't wrap. Returns the number of steps pending.
  int AdvanceActiveLeafWithoutWrapping(int delta);

  std::vector<std::unique_ptr<Widget>> children_;
  size_t active_;

  size_t lines_;
  std::vector<size_t> lines_per_child_;
};

}  // namespace editor
}  // namespace afc

#endif  // __AFC_EDITOR_BUFFER_TREE_HORIZONTAL_H__
