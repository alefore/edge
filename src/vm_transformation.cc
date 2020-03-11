#include "src/vm_transformation.h"

#include <list>
#include <memory>

#include "src/char_buffer.h"
#include "src/modifiers.h"
#include "src/transformation.h"
#include "src/transformation/composite.h"
#include "src/transformation/delete.h"
#include "src/transformation/insert.h"
#include "src/transformation/noop.h"
#include "src/transformation/set_position.h"
#include "src/transformation/type.h"
#include "src/vm/public/callbacks.h"
#include "src/vm/public/constant_expression.h"
#include "src/vm/public/function_call.h"
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
namespace {
class FunctionTransformation : public CompositeTransformation {
 public:
  FunctionTransformation(std::unique_ptr<vm::Value> function)
      : function_(std::move(function)) {
    CHECK(function_ != nullptr);
  }

  std::wstring Serialize() const override {
    return L"FunctionTransformation()";
  }

  futures::Value<Output> Apply(Input input) const override {
    std::vector<std::unique_ptr<vm::Value>> args;
    args.emplace_back(VMTypeMapper<std::shared_ptr<Input>>::New(
        std::make_shared<Input>(input)));
    return futures::Transform(
        vm::Call(*function_, std::move(args),
                 [buffer = input.buffer](std::function<void()> callback) {
                   buffer->work_queue()->Schedule(std::move(callback));
                 }),
        [](std::unique_ptr<Value> value) {
          return std::move(
              *VMTypeMapper<std::shared_ptr<Output>>::get(value.get()));
        });
  }

  std::unique_ptr<CompositeTransformation> Clone() const override {
    return std::make_unique<FunctionTransformation>(
        std::make_unique<Value>(*function_));
  }

 private:
  const std::unique_ptr<vm::Value> function_;
};
}  // namespace
void RegisterTransformations(EditorState* editor,
                             vm::Environment* environment) {
  environment->DefineType(L"Transformation",
                          std::make_unique<vm::ObjectType>(L"Transformation"));

  environment->Define(
      L"FunctionTransformation",
      vm::Value::NewFunction(
          {VMTypeMapper<editor::Transformation*>::vmtype,
           VMType::Function(
               {VMTypeMapper<
                    std::shared_ptr<CompositeTransformation::Output>>::vmtype,
                VMTypeMapper<
                    std::shared_ptr<CompositeTransformation::Input>>::vmtype})},
          [](vector<unique_ptr<vm::Value>> args) {
            CHECK_EQ(args.size(), 1ul);
            return vm::VMTypeMapper<editor::Transformation*>::New(
                NewTransformation(Modifiers(),
                                  std::make_unique<FunctionTransformation>(
                                      std::move(args[0])))
                    .release());
          }));
  transformation::RegisterInsert(editor, environment);
  transformation::RegisterDelete(environment);
  transformation::RegisterSetPosition(environment);
  RegisterNoopTransformation(environment);
  RegisterCompositeTransformation(environment);
}
}  // namespace editor
}  // namespace afc
