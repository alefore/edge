#include "src/transformation/noop.h"

#include "src/vm_transformation.h"

namespace afc::editor {
namespace {
class NoopTransformation : public Transformation {
 public:
  static void Register(vm::Environment* environment) {
    environment->Define(
        L"NoopTransformation",
        vm::Value::NewFunction(
            {vm::VMType::Void()}, [](vector<unique_ptr<vm::Value>> args) {
              CHECK(args.empty());
              return vm::VMTypeMapper<editor::Transformation*>::New(
                  std::make_unique<NoopTransformation>().release());
            }));
  }

  std::wstring Serialize() const { return L"NoopTransformation();"; }

  void Apply(Result*) const override {}

  std::unique_ptr<Transformation> Clone() const override {
    return NewNoopTransformation();
  }
};
}  // namespace

std::unique_ptr<Transformation> NewNoopTransformation() {
  return std::make_unique<NoopTransformation>();
}

void RegisterNoopTransformation(vm::Environment* environment) {
  NoopTransformation::Register(environment);
}
}  // namespace afc::editor
