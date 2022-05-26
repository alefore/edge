#include "src/transformation/noop.h"

#include "src/futures/futures.h"
#include "src/transformation/composite.h"
#include "src/transformation/vm.h"
#include "src/vm/public/types.h"

namespace afc::editor {
using language::MakeNonNullUnique;
using language::NonNull;

namespace gc = language::gc;
namespace transformation {
namespace {
class Noop : public CompositeTransformation {
 public:
  static void Register(language::gc::Pool& pool, vm::Environment& environment) {
    environment.Define(
        L"NoopTransformation",
        vm::NewCallback(pool, vm::PurityType::kPure, []() {
          return MakeNonNullUnique<Variant>(NewNoopTransformation());
        }));
  }

  std::wstring Serialize() const override { return L"NoopTransformation();"; }
  futures::Value<Output> Apply(Input) const override {
    return futures::Past(Output());
  }
};
}  // namespace
}  // namespace transformation

NonNull<std::unique_ptr<CompositeTransformation>> NewNoopTransformation() {
  return NonNull<std::unique_ptr<transformation::Noop>>();
}

void RegisterNoopTransformation(gc::Pool& pool, vm::Environment& environment) {
  transformation::Noop::Register(pool, environment);
}
}  // namespace afc::editor
