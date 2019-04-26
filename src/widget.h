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

  virtual std::unique_ptr<OutputProducer> CreateOutputProducer() = 0;

  virtual void SetSize(size_t lines, size_t columns) = 0;
  virtual size_t lines() const = 0;
  virtual size_t columns() const = 0;
  virtual size_t MinimumLines() = 0;
};

std::ostream& operator<<(std::ostream& os, const Widget& lc);

}  // namespace editor
}  // namespace afc

#endif  // __AFC_EDITOR_WIDGET_H__
