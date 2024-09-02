#ifndef __AFC_EDITOR_TRANSFORMATION_PASTE_H__
#define __AFC_EDITOR_TRANSFORMATION_PASTE_H__

#include <string>

#include "src/transformation/composite.h"

namespace afc::editor::transformation {
class Paste : public CompositeTransformation {
 public:
  Paste() = default;
  std::wstring Serialize() const override;
  futures::Value<Output> Apply(Input input) const override;
};
}  // namespace afc::editor::transformation

#endif  // __AFC_EDITOR_TRANSFORMATION_PASTE_H__
