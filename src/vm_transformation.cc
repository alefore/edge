#include "src/vm_transformation.h"

#include <list>
#include <memory>

#include "src/buffer.h"
#include "src/char_buffer.h"
#include "src/language/safe_types.h"
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
using language::NonNull;
namespace vm {
const VMType VMTypeMapper<editor::transformation::Variant*>::vmtype =
    VMType::ObjectType(L"Transformation");

editor::transformation::Variant*
VMTypeMapper<editor::transformation::Variant*>::get(Value* value) {
  CHECK(value != nullptr);
  CHECK(value->type.type == VMType::OBJECT_TYPE);
  CHECK(value->type.object_type == L"Transformation");
  CHECK(value->user_value != nullptr);
  return static_cast<editor::transformation::Variant*>(value->user_value.get());
}

NonNull<Value::Ptr> VMTypeMapper<editor::transformation::Variant*>::New(
    editor::transformation::Variant* value) {
  return Value::NewObject(
      L"Transformation", shared_ptr<void>(value, [](void* v) {
        delete static_cast<editor::transformation::Variant*>(v);
      }));
}
}  // namespace vm
namespace editor {
namespace {
using language::Error;
using language::MakeNonNullUnique;
using language::Success;

class FunctionTransformation : public CompositeTransformation {
 public:
  FunctionTransformation(NonNull<std::unique_ptr<vm::Value>> function)
      : function_(std::move(function)) {}

  std::wstring Serialize() const override {
    return L"FunctionTransformation()";
  }

  futures::Value<Output> Apply(Input input) const override {
    std::vector<NonNull<std::unique_ptr<vm::Value>>> args;
    args.emplace_back(
        VMTypeMapper<std::shared_ptr<editor::CompositeTransformation::Input>>::
            New(std::make_shared<Input>(input)));
    return vm::Call(*function_, std::move(args),
                    [buffer = input.buffer](std::function<void()> callback) {
                      buffer->work_queue()->Schedule(std::move(callback));
                    })
        .Transform([](NonNull<std::unique_ptr<Value>> value) {
          return Success(std::move(
              *VMTypeMapper<std::shared_ptr<Output>>::get(value.get())));
        })
        .ConsumeErrors([](Error) { return futures::Past(Output()); });
  }

  std::unique_ptr<CompositeTransformation> Clone() const override {
    return std::make_unique<FunctionTransformation>(
        MakeNonNullUnique<Value>(*function_));
  }

 private:
  const NonNull<std::unique_ptr<vm::Value>> function_;
};
}  // namespace
void RegisterTransformations(EditorState* editor,
                             vm::Environment* environment) {
  environment->DefineType(L"Transformation",
                          std::make_unique<vm::ObjectType>(L"Transformation"));

  environment->Define(
      L"FunctionTransformation",
      vm::Value::NewFunction(
          {VMTypeMapper<editor::transformation::Variant*>::vmtype,
           VMType::Function(
               {VMTypeMapper<
                    std::shared_ptr<CompositeTransformation::Output>>::vmtype,
                VMTypeMapper<
                    std::shared_ptr<CompositeTransformation::Input>>::vmtype})},
          [](std::vector<NonNull<std::unique_ptr<vm::Value>>> args) {
            CHECK_EQ(args.size(), 1ul);
            // TODO(easy, 2022-04-24): Remove get_unique.
            return VMTypeMapper<editor::transformation::Variant*>::New(
                std::make_unique<transformation::Variant>(
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
