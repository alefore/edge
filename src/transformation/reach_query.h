#ifndef __AFC_EDITOR_TRANSFORMATION_REACH_QUERY_H__
#define __AFC_EDITOR_TRANSFORMATION_REACH_QUERY_H__

#include <string>

#include "src/language/lazy_string/lazy_string.h"
#include "src/transformation/composite.h"

namespace afc::editor::transformation {
class ReachQueryTransformation : public CompositeTransformation {
  const language::lazy_string::LazyString query_;

 public:
  ReachQueryTransformation(language::lazy_string::LazyString query);
  std::wstring Serialize() const override;
  futures::Value<Output> Apply(Input input) const override;
};
}  // namespace afc::editor::transformation

#endif  // __AFC_EDITOR_TRANSFORMATION_REACH_QUERY_H__
