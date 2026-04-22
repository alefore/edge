#ifndef __AFC_EDITOR_TRANSFORMATION_SWITCH_CASE_H__
#define __AFC_EDITOR_TRANSFORMATION_SWITCH_CASE_H__

#include <memory>

#include "src/modifiers.h"
#include "src/transformation.h"

namespace afc::editor {
class SwitchCaseTransformation : public CompositeTransformation {
 public:
  std::wstring Serialize() const override;
  futures::Value<Output> Apply(Input input) const override;
};
}  // namespace afc::editor

#endif  // __AFC_EDITOR_TRANSFORMATION_SWITCH_CASE_H__
