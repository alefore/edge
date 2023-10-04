// TODO(trivial, 2023-09-29): Delete this dead code. But probably want to
// improve the tree-scroll behavior first.

#ifndef __AFC_EDITOR_TRANSFORMATION_TREE_NAVIGATE_H__
#define __AFC_EDITOR_TRANSFORMATION_TREE_NAVIGATE_H__

#include <memory>

#include "src/transformation.h"
#include "src/vm/environment.h"

namespace afc::editor {
class TreeNavigate : public CompositeTransformation {
  std::wstring Serialize() const override;
  futures::Value<Output> Apply(Input input) const override;
};
}  // namespace afc::editor

#endif  // __AFC_EDITOR_TRANSFORMATION_TREE_NAVIGATE_H__
