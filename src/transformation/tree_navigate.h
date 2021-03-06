#ifndef __AFC_EDITOR_TRANSFORMATION_TREE_NAVIGATE_H__
#define __AFC_EDITOR_TRANSFORMATION_TREE_NAVIGATE_H__

#include <memory>

#include "src/transformation.h"
#include "src/vm/public/environment.h"

namespace afc::editor {
class TreeNavigate : public CompositeTransformation {
  std::wstring Serialize() const override;
  futures::Value<Output> Apply(Input input) const override;
  std::unique_ptr<CompositeTransformation> Clone() const override;
};
}  // namespace afc::editor

#endif  // __AFC_EDITOR_TRANSFORMATION_TREE_NAVIGATE_H__
