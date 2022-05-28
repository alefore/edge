#include "src/transformation/vm.h"

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
using language::MakeNonNullShared;
using language::NonNull;

namespace gc = language::gc;
namespace vm {
template <>
const VMType VMTypeMapper<
    NonNull<std::shared_ptr<editor::transformation::Variant>>>::vmtype =
    VMType::ObjectType(VMTypeObjectTypeName(L"Transformation"));
}  // namespace vm
namespace editor {
using language::MakeNonNullUnique;
namespace {
using language::Error;
using language::Success;
using vm::PurityType;
using vm::VMType;
using vm::VMTypeMapper;

class FunctionTransformation : public CompositeTransformation {
 public:
  FunctionTransformation(gc::Pool& pool, gc::Root<vm::Value> function)
      : pool_(pool), function_(std::move(function)) {}

  std::wstring Serialize() const override {
    return L"FunctionTransformation()";
  }

  futures::Value<Output> Apply(Input input) const override {
    std::vector<gc::Root<vm::Value>> args;
    args.emplace_back(
        VMTypeMapper<
            NonNull<std::shared_ptr<editor::CompositeTransformation::Input>>>::
            New(pool_, MakeNonNullShared<Input>(input)));
    return vm::Call(pool_, function_.ptr().value(), std::move(args),
                    [work_queue = input.buffer.work_queue()](
                        std::function<void()> callback) {
                      work_queue->Schedule(std::move(callback));
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
  const gc::Root<vm::Value> function_;
};
}  // namespace
void RegisterTransformations(gc::Pool& pool, vm::Environment& environment) {
  environment.DefineType(MakeNonNullUnique<vm::ObjectType>(
      VMTypeMapper<
          NonNull<std::shared_ptr<editor::transformation::Variant>>>::vmtype));

  environment.Define(
      L"FunctionTransformation",
      vm::Value::NewFunction(
          pool, PurityType::kPure,
          {VMTypeMapper<NonNull<
               std::shared_ptr<editor::transformation::Variant>>>::vmtype,
           VMType::Function(
               {VMTypeMapper<NonNull<
                    std::shared_ptr<CompositeTransformation::Output>>>::vmtype,
                VMTypeMapper<NonNull<std::shared_ptr<
                    CompositeTransformation::Input>>>::vmtype})},
          [&pool](std::vector<gc::Root<vm::Value>> args) {
            CHECK_EQ(args.size(), 1ul);
            return VMTypeMapper<
                NonNull<std::shared_ptr<editor::transformation::Variant>>>::
                New(pool, MakeNonNullUnique<transformation::Variant>(
                              MakeNonNullUnique<FunctionTransformation>(
                                  pool, std::move(args[0]))));
          }));

  transformation::RegisterInsert(pool, environment);
  transformation::RegisterDelete(pool, environment);
  transformation::RegisterSetPosition(pool, environment);
  RegisterNoopTransformation(pool, environment);
  RegisterCompositeTransformation(pool, environment);
}
}  // namespace editor
}  // namespace afc