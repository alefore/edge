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
  enum class BuffersVisible { kAll, kActive };

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

  // Where should the new buffer be shown?
  enum class InsertionType {
    // Searches the tree of widgets to see if a leaf for the current buffer
    // already exists. The first such widget found is selected as active.
    // Otherwise, creates a new buffer widget.
    kSearchOrCreate,
    // Create a new buffer widget displaying the buffer.
    kCreate,
    // Show the buffer in the current buffer widget.
    kReuseCurrent,
    // Don't actually insert the children.
    kSkip
  };
  void InsertChildren(std::shared_ptr<OpenBuffer> buffer,
                      InsertionType insertion_type);

  void PushChildren(std::unique_ptr<Widget> children);
  size_t children_count() const;

  void RemoveActiveLeaf();
  void AddSplit();
  void ZoomToActiveLeaf();

  BuffersVisible buffers_visible() const;
  void SetBuffersVisible(BuffersVisible buffers_visible);

 private:
  enum class LeafSearchResult { kFound, kNotFound };
  LeafSearchResult SelectLeafFor(OpenBuffer* buffer);

  void RecomputeLinesPerChild();

  // Doesn't wrap. Returns the number of steps pending.
  int AdvanceActiveLeafWithoutWrapping(int delta);

  BuffersVisible buffers_visible_ = BuffersVisible::kAll;

  std::vector<std::unique_ptr<Widget>> children_;
  size_t active_;

  size_t lines_;
  std::vector<size_t> lines_per_child_;
};

}  // namespace editor
}  // namespace afc

#endif  // __AFC_EDITOR_BUFFER_TREE_HORIZONTAL_H__
