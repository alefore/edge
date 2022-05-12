#include "src/vm_transformation.h"

#include <list>
#include <memory>

#include "src/buffer.h"
#include "src/char_buffer.h"
#include "src/editor.h"
#include "src/language/gc.h"
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

namespace gc = language::gc;
namespace vm {
const VMType VMTypeMapper<editor::transformation::Variant*>::vmtype =
    VMType::ObjectType(L"Transformation");

editor::transformation::Variant*
VMTypeMapper<editor::transformation::Variant*>::get(Value& value) {
  CHECK(value.type.type == VMType::OBJECT_TYPE);
  CHECK(value.type.object_type == L"Transformation");
  CHECK(value.user_value != nullptr);
  return static_cast<editor::transformation::Variant*>(value.user_value.get());
}

NonNull<Value::Ptr> VMTypeMapper<editor::transformation::Variant*>::New(
    gc::Pool& pool, editor::transformation::Variant* value) {
  return Value::NewObject(
      pool, L"Transformation", shared_ptr<void>(value, [](void* v) {
        delete static_cast<editor::transformation::Variant*>(v);
      }));
}
}  // namespace vm
namespace editor {
using language::MakeNonNullUnique;
namespace {
using language::Error;
using language::Success;

class FunctionTransformation : public CompositeTransformation {
 public:
  FunctionTransformation(gc::Pool& pool,
                         NonNull<std::unique_ptr<vm::Value>> function)
      : pool_(pool), function_(std::move(function)) {}

  std::wstring Serialize() const override {
    return L"FunctionTransformation()";
  }

  futures::Value<Output> Apply(Input input) const override {
    std::vector<NonNull<std::unique_ptr<vm::Value>>> args;
    args.emplace_back(
        VMTypeMapper<std::shared_ptr<editor::CompositeTransformation::Input>>::
            New(pool_, std::make_shared<Input>(input)));
    return vm::Call(pool_, *function_, std::move(args),
                    [work_queue = input.buffer.work_queue()](
                        std::function<void()> callback) {
                      work_queue->Schedule(std::move(callback));
                    })
        .Transform([](NonNull<std::unique_ptr<Value>> value) {
          return Success(std::move(
              *VMTypeMapper<std::shared_ptr<Output>>::get(value.value())));
        })
        .ConsumeErrors([](Error) { return futures::Past(Output()); });
  }

 private:
  gc::Pool& pool_;
  const NonNull<std::unique_ptr<vm::Value>> function_;
};
}  // namespace
// TODO(easy, 2022-05-12): Receive editor/environment by ref.
void RegisterTransformations(EditorState* editor,
                             vm::Environment* environment) {
  environment->DefineType(L"Transformation",
                          MakeNonNullUnique<vm::ObjectType>(L"Transformation"));
  language::gc::Pool& pool = editor->gc_pool();
  environment->Define(
      L"FunctionTransformation",
      vm::Value::NewFunction(
          pool,
          {VMTypeMapper<editor::transformation::Variant*>::vmtype,
           VMType::Function(
               {VMTypeMapper<
                    std::shared_ptr<CompositeTransformation::Output>>::vmtype,
                VMTypeMapper<
                    std::shared_ptr<CompositeTransformation::Input>>::vmtype})},
          [editor](std::vector<NonNull<std::unique_ptr<vm::Value>>> args) {
            CHECK_EQ(args.size(), 1ul);
            return VMTypeMapper<editor::transformation::Variant*>::New(
                editor->gc_pool(),
                std::make_unique<transformation::Variant>(
                    MakeNonNullUnique<FunctionTransformation>(
                        editor->gc_pool(), std::move(args[0])))
                    .release());
          }));
  transformation::RegisterInsert(editor, environment);
  transformation::RegisterDelete(pool, environment);
  transformation::RegisterSetPosition(pool, environment);
  RegisterNoopTransformation(pool, environment);
  RegisterCompositeTransformation(pool, environment);
}
}  // namespace editor
}  // namespace afc
