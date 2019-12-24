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

template <>
struct VMTypeMapper<editor::InsertOptions*> {
  static editor::InsertOptions* get(Value* value) {
    CHECK(value != nullptr);
    CHECK(value->type.type == VMType::OBJECT_TYPE);
    CHECK(value->type.object_type == L"InsertTextBuilder");
    CHECK(value->user_value != nullptr);
    return static_cast<editor::InsertOptions*>(value->user_value.get());
  }
  static Value::Ptr New(editor::InsertOptions* value) {
    return Value::NewObject(L"InsertTextBuilder",
                            std::shared_ptr<void>(value, [](void* v) {
                              delete static_cast<editor::InsertOptions*>(v);
                            }));
  }
  static const VMType vmtype;
};

const VMType VMTypeMapper<editor::InsertOptions*>::vmtype =
    VMType::ObjectType(L"InsertTextBuilder");

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
  using vm::NewCallback;
  using vm::ObjectType;
  using vm::Value;
  using vm::VMType;
  auto transformation = std::make_unique<ObjectType>(L"Transformation");

  environment->DefineType(L"Transformation", std::move(transformation));

  auto insert_text_builder = std::make_unique<ObjectType>(L"InsertTextBuilder");

  environment->Define(L"InsertTextBuilder",
                      NewCallback(std::function<InsertOptions*()>(
                          []() { return new InsertOptions(); })));

  insert_text_builder->AddField(
      L"set_modifiers",
      NewCallback(
          std::function<void(InsertOptions*, std::shared_ptr<Modifiers>)>(
              [editor](InsertOptions* options,
                       std::shared_ptr<Modifiers> modifiers) {
                options->modifiers = *modifiers;
              })));

  insert_text_builder->AddField(
      L"set_position",
      NewCallback(std::function<void(InsertOptions*, LineColumn)>(
          [editor](InsertOptions* options, LineColumn position) {
            options->position = position;
          })));

  insert_text_builder->AddField(
      L"set_text",
      NewCallback(std::function<void(InsertOptions*, wstring)>(
          [editor](InsertOptions* options, wstring text) {
            auto buffer_to_insert =
                std::make_shared<OpenBuffer>(editor, L"- text inserted");
            buffer_to_insert->AppendToLastLine(NewLazyString(std::move(text)));
            options->buffer_to_insert = buffer_to_insert;
          })));

  insert_text_builder->AddField(
      L"build", NewCallback(std::function<Transformation*(InsertOptions*)>(
                    [editor](InsertOptions* options) {
                      return NewInsertBufferTransformation(*options).release();
                    })));

  environment->DefineType(L"InsertTextBuilder", std::move(insert_text_builder));

  RegisterDeleteTransformation(environment);
  RegisterNoopTransformation(environment);
  RegisterSetPositionTransformation(environment);
}
}  // namespace editor
}  // namespace afc
