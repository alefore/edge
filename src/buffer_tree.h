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

class BufferTree {
 public:
  enum class Type { kLeaf, /*kVertical,*/ kHorizontal };

  static BufferTree NewLeaf(std::weak_ptr<OpenBuffer> buffer);
  static BufferTree NewHorizontal(Tree<BufferTree> buffers,
                                  size_t active_index);

  Type type() const { return type_; }

  void AddHorizontalSplit();

  void SetActiveLeafBuffer(std::shared_ptr<OpenBuffer> buffer);
  void SetActiveLeaf(size_t position);
  std::shared_ptr<OpenBuffer> LockActiveLeaf() const;

  void RemoveActiveLeaf();
  std::vector<BufferTree*> FindRouteToActiveLeaf();

  BufferTree* FindActiveLeaf();
  const BufferTree* FindActiveLeaf() const;
  void AdvanceActiveLeaf(int delta);

  template <typename T>
  void ForEach(T callback) {
    switch (type_) {
      case Type::kLeaf:
        return;
      case Type::kHorizontal:
        for (size_t i = 0; i < children_.size(); i++) {
          callback(&children_[i], i == active_);
        }
        return;
    }
  }

  size_t CountLeafs() const;

  wstring ToString() const;

 private:
  BufferTree() = default;

  int InternalAdvanceActiveLeaf(int delta);

  Type type_ = Type::kLeaf;

  // Ignored if type != kLeaf.
  std::weak_ptr<OpenBuffer> leaf_;

  // Ignored if type == kLeaf.
  Tree<BufferTree> children_;
  size_t active_;
};

std::ostream& operator<<(std::ostream& os, const BufferTree& lc);

}  // namespace editor
}  // namespace afc

#endif  // __AFC_EDITOR_BUFFER_TREE_H__
