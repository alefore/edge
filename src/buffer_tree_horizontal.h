#ifndef __AFC_EDITOR_BUFFER_TREE_HORIZONTAL_H__
#define __AFC_EDITOR_BUFFER_TREE_HORIZONTAL_H__

#include <list>
#include <memory>

#include "src/buffers_list.h"
#include "src/output_producer.h"
#include "src/vm/public/environment.h"
#include "src/widget.h"

namespace afc {
namespace editor {

class BufferTree : public SelectingWidget {
 public:
  BufferWidget* GetActiveLeaf() override;
  const BufferWidget* GetActiveLeaf() const;

  void SetSize(LineColumnDelta size) override;
  LineColumnDelta size() const override;
  void RemoveBuffer(OpenBuffer* buffer) override;

  size_t count() const override;
  // Returns the currently selected index. An invariant is that it will be
  // smaller than count.
  size_t index() const override;
  void set_index(size_t new_index) override;
  void AddChild(std::unique_ptr<Widget> widget) override;

  // Overrides from DelegatingWidget.
  Widget* Child() override;
  void SetChild(std::unique_ptr<Widget> widget) override;
  void WrapChild(std::function<std::unique_ptr<Widget>(std::unique_ptr<Widget>)>
                     callback) override;

  size_t CountLeaves() const override;

  // Advances the active leaf (recursing down into child containers) by this
  // number of positions.
  //
  // Doesn't wrap. Returns the number of steps pending.
  int AdvanceActiveLeafWithoutWrapping(int delta) override;

  void SetActiveLeavesAtStart() override;

  void RemoveActiveLeaf();

 protected:
  BufferTree(std::unique_ptr<Widget> children);
  BufferTree(std::vector<std::unique_ptr<Widget>> children, size_t active);

  std::vector<std::unique_ptr<Widget>> children_;
  size_t active_;

  LineColumnDelta size_;
};

class BufferTreeHorizontal : public BufferTree {
 public:
  BufferTreeHorizontal(std::unique_ptr<Widget> children);

  BufferTreeHorizontal(std::vector<std::unique_ptr<Widget>> children,
                       size_t active);

  wstring Name() const;
  wstring ToString() const override;

  std::unique_ptr<OutputProducer> CreateOutputProducer() override;

  void SetSize(LineColumnDelta size) override;
  LineNumberDelta MinimumLines() override;

 private:
  std::vector<LineNumberDelta> lines_per_child_;
};

class BufferTreeVertical : public BufferTree {
 public:
  BufferTreeVertical(std::unique_ptr<Widget> children);

  BufferTreeVertical(std::vector<std::unique_ptr<Widget>> children,
                     size_t active);

  wstring Name() const;
  wstring ToString() const override;

  std::unique_ptr<OutputProducer> CreateOutputProducer() override;

  void SetSize(LineColumnDelta size) override;
  LineNumberDelta MinimumLines() override;

 private:
  std::vector<ColumnNumberDelta> columns_per_child_;
};

}  // namespace editor
}  // namespace afc

#endif  // __AFC_EDITOR_BUFFER_TREE_HORIZONTAL_H__
