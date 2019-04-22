#ifndef __AFC_EDITOR_BUFFER_TREE_H__
#define __AFC_EDITOR_BUFFER_TREE_H__

#include <list>
#include <memory>

#include "src/lazy_string.h"
#include "src/output_producer.h"
#include "src/parse_tree.h"
#include "src/tree.h"
#include "src/vm/public/environment.h"

namespace afc {
namespace editor {

class BufferTreeLeaf;

class BufferTree {
 public:
  ~BufferTree() = default;

  virtual BufferTreeLeaf* GetActiveLeaf() = 0;

  virtual void SetActiveLeafBuffer(std::shared_ptr<OpenBuffer> buffer) = 0;

  virtual void SetActiveLeaf(size_t position) = 0;

  // Move the active leaf by this number of positions.
  virtual void AdvanceActiveLeaf(int delta) = 0;

  virtual size_t CountLeafs() const = 0;

  virtual wstring Name() const = 0;
  virtual wstring ToString() const = 0;

  virtual std::unique_ptr<OutputProducer> CreateOutputProducer() = 0;

  virtual void SetLines(size_t) = 0;
  virtual size_t lines() const = 0;
  virtual size_t MinimumLines() = 0;
};

std::ostream& operator<<(std::ostream& os, const BufferTree& lc);

}  // namespace editor
}  // namespace afc

#endif  // __AFC_EDITOR_BUFFER_TREE_H__
