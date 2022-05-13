#include "src/transformation/noop.h"

#include "src/futures/futures.h"
#include "src/transformation/composite.h"
#include "src/vm_transformation.h"

namespace afc::editor {
namespace {
using language::MakeNonNullUnique;
using language::NonNull;

namespace gc = language::gc;

class Noop : public CompositeTransformation {
 public:
  static void Register(language::gc::Pool& pool, vm::Environment& environment) {
    environment.Define(
        L"NoopTransformation",
        vm::Value::NewFunction(
            pool, {vm::VMType::Void()},
            [&pool](std::vector<gc::Root<vm::Value>> args) {
              CHECK(args.empty());
              return vm::VMTypeMapper<editor::transformation::Variant*>::New(
                  pool, std::make_unique<transformation::Variant>(
                            transformation::Variant(NewNoopTransformation()))
                            .release());
            }));
  }

  std::wstring Serialize() const override { return L"NoopTransformation();"; }
  futures::Value<Output> Apply(Input) const override {
    return futures::Past(Output());
  }
};
}  // namespace

NonNull<std::unique_ptr<CompositeTransformation>> NewNoopTransformation() {
  return NonNull<std::unique_ptr<Noop>>();
}

void RegisterNoopTransformation(gc::Pool& pool, vm::Environment& environment) {
  Noop::Register(pool, environment);
}
}  // namespace afc::editor
