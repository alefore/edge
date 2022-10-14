#ifndef __AFC_EDITOR_TRANSFORMATION_REACH_QUERY_H__
#define __AFC_EDITOR_TRANSFORMATION_REACH_QUERY_H__

#include <string>

#include "src/transformation/composite.h"

namespace afc::editor::transformation {
class ReachQueryTransformation : public CompositeTransformation {
 public:
  ReachQueryTransformation(std::wstring query);
  std::wstring Serialize() const override;
  futures::Value<Output> Apply(Input input) const override;

 private:
  const std::wstring query_;
};
}  // namespace afc::editor::transformation

#endif  // __AFC_EDITOR_TRANSFORMATION_REACH_QUERY_H__
