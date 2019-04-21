#ifndef __AFC_EDITOR_BUFFER_TREE_H__
#define __AFC_EDITOR_BUFFER_TREE_H__

#include <list>
#include <memory>

#include "src/lazy_string.h"
#include "src/output_receiver.h"
#include "src/parse_tree.h"
#include "src/tree.h"
#include "src/vm/public/environment.h"

namespace afc {
namespace editor {

struct BufferTree {
  enum class Type { kLeaf, /*kVertical,*/ kHorizontal };

  Type type = Type::kLeaf;

  // Ignored if type != kLeaf.
  std::weak_ptr<OpenBuffer> leaf;

  // Ignored if type == kLeaf.
  Tree<BufferTree> children;
  size_t active;

  // TODO: Turn BufferTree into a class, make this not static.
  static void RemoveActiveLeaf(BufferTree* tree);
};

std::ostream& operator<<(std::ostream& os, const BufferTree& lc);

std::vector<BufferTree*> FindRouteToActiveLeaf(BufferTree* tree);
BufferTree* FindActiveLeaf(BufferTree* tree);
const BufferTree* FindActiveLeaf(const BufferTree* tree);
void RemoveActiveLeaf(BufferTree* tree);
void AdvanceActiveLeaf(BufferTree* tree, int delta);

}  // namespace editor
}  // namespace afc

#endif  // __AFC_EDITOR_BUFFER_TREE_H__
