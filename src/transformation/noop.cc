#include "src/transformation/noop.h"

#include "src/futures/futures.h"
#include "src/transformation/composite.h"
#include "src/vm_transformation.h"

namespace afc::editor {
namespace {
class Noop : public CompositeTransformation {
 public:
  static void Register(vm::Environment* environment) {
    environment->Define(
        L"NoopTransformation",
        vm::Value::NewFunction(
            {vm::VMType::Void()}, [](vector<unique_ptr<vm::Value>> args) {
              CHECK(args.empty());
              return vm::VMTypeMapper<editor::Transformation*>::New(
                  NewNoopTransformation().release());
            }));
  }

  std::wstring Serialize() const override { return L"NoopTransformation();"; }
  futures::Value<Output> Apply(Input) const override {
    return futures::Past(Output());
  }
  std::unique_ptr<CompositeTransformation> Clone() const override {
    return std::make_unique<Noop>();
  }
};
}  // namespace

std::unique_ptr<Transformation> NewNoopTransformation() {
  return NewTransformation(Modifiers(), std::make_unique<Noop>());
}

void RegisterNoopTransformation(vm::Environment* environment) {
  Noop::Register(environment);
}
}  // namespace afc::editor
