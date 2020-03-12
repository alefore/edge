#include "src/transformation/type.h"

#include "src/futures/futures.h"
#include "src/transformation/composite.h"
#include "src/vm_transformation.h"

namespace afc::editor::transformation {

namespace {
class Adapter : public Transformation {
 public:
  Adapter(BaseTransformation base_transformation)
      : base_transformation_(std::move(base_transformation)) {}

  futures::Value<Result> Apply(const Input& input) const override {
    return std::visit([&](auto& value) { return ApplyBase(value, input); },
                      base_transformation_);
  }

  std::unique_ptr<Transformation> Clone() const override {
    return Build(base_transformation_);
  }

 private:
  const BaseTransformation base_transformation_;
};
}  // namespace

std::unique_ptr<Transformation> Build(BaseTransformation base_transformation) {
  return std::make_unique<Adapter>(std::move(base_transformation));
}
}  // namespace afc::editor::transformation
