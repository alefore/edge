#include "src/transformation/vm.h"

#include <list>
#include <memory>

#include "src/buffer.h"
#include "src/editor.h"
#include "src/language/gc.h"
#include "src/language/lazy_string/char_buffer.h"
#include "src/language/lazy_string/lazy_string.h"
#include "src/language/safe_types.h"
#include "src/modifiers.h"
#include "src/transformation.h"
#include "src/transformation/composite.h"
#include "src/transformation/delete.h"
#include "src/transformation/insert.h"
#include "src/transformation/noop.h"
#include "src/transformation/set_position.h"
#include "src/transformation/type.h"
#include "src/vm/callbacks.h"
#include "src/vm/constant_expression.h"
#include "src/vm/function_call.h"
#include "src/vm/types.h"

using afc::concurrent::WorkQueue;
using afc::language::Error;
using afc::language::MakeNonNullShared;
using afc::language::MakeNonNullUnique;
using afc::language::NonNull;
using afc::language::OnceOnlyFunction;
using afc::language::Success;
using afc::language::lazy_string::LazyString;
using afc::language::lazy_string::NonEmptySingleLine;
using afc::language::lazy_string::SingleLine;
using afc::vm::GetVMType;
using afc::vm::kPurityTypePure;
using afc::vm::VMTypeMapper;

namespace afc {

namespace gc = language::gc;
namespace vm {
template <>
const types::ObjectName VMTypeMapper<NonNull<
    std::shared_ptr<editor::transformation::Variant>>>::object_type_name =
    types::ObjectName{LazyString{L"Transformation"}};
}  // namespace vm
namespace editor {
namespace {

class FunctionTransformation : public CompositeTransformation {
 public:
  FunctionTransformation(gc::Pool& pool, vm::Value& function)
      : pool_(pool), function_(function) {}

  std::wstring Serialize() const override {
    return L"FunctionTransformation()";
  }

  futures::Value<Output> Apply(Input input) const override {
    std::vector<gc::Root<vm::Value>> args;
    args.emplace_back(
        VMTypeMapper<
            NonNull<std::shared_ptr<editor::CompositeTransformation::Input>>>::
            New(pool_, MakeNonNullShared<Input>(input)));
    return vm::Call(pool_, function_, std::move(args),
                    [work_queue = input.buffer.work_queue()](
                        OnceOnlyFunction<void()> callback) {
                      work_queue->Schedule(
                          WorkQueue::Callback{.callback = std::move(callback)});
                    })
        .Transform([](gc::Root<vm::Value> value) {
          return Success(
              std::move(VMTypeMapper<NonNull<std::shared_ptr<Output>>>::get(
                            value.ptr().value())
                            .value()));
        })
        .ConsumeErrors([](Error) { return futures::Past(Output()); });
  }

 private:
  gc::Pool& pool_;
  vm::Value& function_;
};
}  // namespace
void RegisterTransformations(gc::Pool& pool, vm::Environment& environment) {
  environment.DefineType(
      vm::ObjectType::New(
          pool, VMTypeMapper<NonNull<std::shared_ptr<
                    editor::transformation::Variant>>>::object_type_name)
          .ptr());

  environment.Define(
      vm::Identifier{NonEmptySingleLine{
          SingleLine{LazyString{L"FunctionTransformation"}}}},
      vm::Value::NewFunction(
          pool, kPurityTypePure,
          GetVMType<NonNull<std::shared_ptr<editor::transformation::Variant>>>::
              vmtype(),
          {vm::types::Function{
              .output = GetVMType<NonNull<
                  std::shared_ptr<CompositeTransformation::Output>>>::vmtype(),
              .inputs = {GetVMType<NonNull<std::shared_ptr<
                  CompositeTransformation::Input>>>::vmtype()}}},
          [&pool](std::vector<gc::Root<vm::Value>> args) {
            CHECK_EQ(args.size(), 1ul);
            gc::Ptr<vm::Value> callback = args[0].ptr();
            std::vector<NonNull<std::shared_ptr<gc::ObjectMetadata>>>
                expansion = {callback.object_metadata()};
            return vm::Value::NewObject(
                pool,
                VMTypeMapper<NonNull<std::shared_ptr<
                    editor::transformation::Variant>>>::object_type_name,
                MakeNonNullUnique<transformation::Variant>(
                    MakeNonNullUnique<FunctionTransformation>(
                        pool, callback.value())),
                [expansion] { return expansion; });
          }));

  transformation::RegisterInsert(pool, environment);
  transformation::RegisterDelete(pool, environment);
  transformation::RegisterSetPosition(pool, environment);
  RegisterNoopTransformation(pool, environment);
  RegisterCompositeTransformation(pool, environment);
}
}  // namespace editor
}  // namespace afc
