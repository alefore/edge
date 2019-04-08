#include "src/vm_transformation.h"

#include <list>
#include <memory>

#include "src/transformation.h"
#include "src/transformation_delete.h"
#include "src/vm/public/callbacks.h"
#include "src/vm/public/types.h"

namespace afc {
namespace vm {

const VMType VMTypeMapper<editor::Transformation*>::vmtype =
    VMType::ObjectType(L"Transformation");

editor::Transformation* VMTypeMapper<editor::Transformation*>::get(
    Value* value) {
  CHECK(value != nullptr);
  CHECK(value->type.type == VMType::OBJECT_TYPE);
  CHECK(value->type.object_type == L"Transformation");
  CHECK(value->user_value != nullptr);
  return static_cast<editor::Transformation*>(value->user_value.get());
}

Value::Ptr VMTypeMapper<editor::Transformation*>::New(
    editor::Transformation* value) {
  return Value::NewObject(L"Transformation",
                          shared_ptr<void>(value, [](void* v) {
                            delete static_cast<editor::Transformation*>(v);
                          }));
}

}  // namespace vm
namespace editor {
void RegisterTransformations(vm::Environment* environment) {
  using vm::NewCallback;
  using vm::ObjectType;
  using vm::Value;
  using vm::VMType;
  auto transformation = std::make_unique<ObjectType>(L"Transformation");

  environment->Define(
      L"TransformationGoToColumn",
      NewCallback(std::function<Transformation*(int)>([](int column) {
        return NewGotoColumnTransformation(column).release();
      })));

  environment->Define(L"TransformationDelete",
                      NewCallback(std::function<Transformation*(Modifiers*)>(
                          [](Modifiers* modifiers) {
                            DeleteOptions options;
                            options.modifiers = *modifiers;
                            return NewDeleteTransformation(options).release();
                          })));

  environment->DefineType(L"Transformation", std::move(transformation));
}
}  // namespace editor
}  // namespace afc
