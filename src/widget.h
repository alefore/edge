#ifndef __AFC_EDITOR_WIDGET_H__
#define __AFC_EDITOR_WIDGET_H__

#include <list>
#include <memory>

#include "src/lazy_string.h"
#include "src/output_producer.h"
#include "src/parse_tree.h"
#include "src/tree.h"
#include "src/vm/public/environment.h"

namespace afc {
namespace editor {

class BufferWidget;

class Widget {
 public:
  ~Widget() = default;

  virtual wstring Name() const = 0;
  virtual wstring ToString() const = 0;

  virtual BufferWidget* GetActiveLeaf() = 0;
  virtual const BufferWidget* GetActiveLeaf() const = 0;

  virtual std::unique_ptr<OutputProducer> CreateOutputProducer() = 0;

  virtual void SetSize(LineColumnDelta size) = 0;
  virtual LineNumberDelta lines() const = 0;
  virtual ColumnNumberDelta columns() const = 0;
  virtual LineNumberDelta MinimumLines() = 0;
  virtual void RemoveBuffer(OpenBuffer* buffer) = 0;

  virtual size_t CountLeaves() const = 0;

  // Advances the active leaf (recursing down into child containers) by this
  // number of positions.
  //
  // Doesn't wrap. Returns the number of steps pending.
  //
  // If the buffer has a single leaf, it should just return delta.
  virtual int AdvanceActiveLeafWithoutWrapping(int delta) = 0;
  virtual void SetActiveLeavesAtStart() = 0;
};

// A widget that contains a child. It typically adds something extra for that
// children, such as a status or a frame.
class DelegatingWidget : public Widget {
 public:
  // Can be nullptr.
  virtual Widget* Child() = 0;
  virtual void SetChild(std::unique_ptr<Widget> widget) = 0;
  virtual void WrapChild(
      std::function<std::unique_ptr<Widget>(std::unique_ptr<Widget>)>
          callback) = 0;
};

// A widget that contains one or more children.
class SelectingWidget : public DelegatingWidget {
 public:
  // Returns the current number of children. Will always return a value greater
  // than 0.
  virtual size_t count() const = 0;
  // Returns the currently selected index. An invariant is that it will be
  // smaller than count.
  virtual size_t index() const = 0;
  virtual void set_index(size_t new_index) = 0;
  virtual void AddChild(std::unique_ptr<Widget> widget) = 0;
};

std::ostream& operator<<(std::ostream& os, const Widget& lc);

}  // namespace editor
}  // namespace afc

#endif  // __AFC_EDITOR_WIDGET_H__
