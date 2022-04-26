#include "src/transformation/noop.h"

#include "src/futures/futures.h"
#include "src/transformation/composite.h"
#include "src/vm_transformation.h"

namespace afc::editor {
namespace {
using language::MakeNonNullUnique;
using language::NonNull;

class Noop : public CompositeTransformation {
 public:
  static void Register(vm::Environment* environment) {
    environment->Define(
        L"NoopTransformation",
        vm::Value::NewFunction(
            {vm::VMType::Void()},
            [](std::vector<NonNull<std::unique_ptr<vm::Value>>> args) {
              CHECK(args.empty());
              return vm::VMTypeMapper<editor::transformation::Variant*>::New(
                  std::make_unique<transformation::Variant>(
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

void RegisterNoopTransformation(vm::Environment* environment) {
  Noop::Register(environment);
}
}  // namespace afc::editor
