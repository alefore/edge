#include "src/vm_transformation.h"

#include <list>
#include <memory>

#include "src/char_buffer.h"
#include "src/modifiers.h"
#include "src/transformation.h"
#include "src/transformation/delete.h"
#include "src/transformation/insert.h"
#include "src/transformation/noop.h"
#include "src/transformation/set_position.h"
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
void RegisterTransformations(EditorState* editor,
                             vm::Environment* environment) {
  environment->DefineType(L"Transformation",
                          std::make_unique<vm::ObjectType>(L"Transformation"));

  RegisterInsertTransformation(editor, environment);
  RegisterDeleteTransformation(environment);
  RegisterNoopTransformation(environment);
  RegisterSetPositionTransformation(environment);
}
}  // namespace editor
}  // namespace afc
