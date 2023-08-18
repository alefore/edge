#ifndef __AFC_EDITOR_TRANSFORMATION_BISECT_H__
#define __AFC_EDITOR_TRANSFORMATION_BISECT_H__

#include <string>

#include "src/transformation/composite.h"

namespace afc::editor::transformation {
class Bisect : public CompositeTransformation {
 public:
  Bisect(Structure structure, std::vector<Direction> directions);
  std::wstring Serialize() const override;
  futures::Value<Output> Apply(Input input) const override;

 private:
  const Structure structure_;
  const std::vector<Direction> directions_;
};
}  // namespace afc::editor::transformation

#endif  // __AFC_EDITOR_TRANSFORMATION_BISECT_H__
