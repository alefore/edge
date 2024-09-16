#ifndef __AFC_EDITOR_TRANSFORMATION_PASTE_H__
#define __AFC_EDITOR_TRANSFORMATION_PASTE_H__

#include <string>

#include "src/fragments.h"
#include "src/transformation/composite.h"

namespace afc::editor::transformation {
class Paste : public CompositeTransformation {
  const FindFragmentQuery query_;

 public:
  Paste() = default;
  Paste(FindFragmentQuery query);
  std::wstring Serialize() const override;
  futures::Value<Output> Apply(Input input) const override;
};
}  // namespace afc::editor::transformation

#endif  // __AFC_EDITOR_TRANSFORMATION_PASTE_H__
