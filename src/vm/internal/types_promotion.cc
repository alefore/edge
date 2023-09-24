#include "src/vm/internal/types_promotion.h"

#include "src/tests/tests.h"
#include "src/vm/public/callbacks.h"
#include "src/vm/public/environment.h"

using afc::language::ValueOrError;
using afc::language::numbers::Number;
using afc::language::numbers::ToString;

namespace afc::vm {
using language::NonNull;
using language::Success;

namespace gc = language::gc;

using PromotionCallback =
    std::function<gc::Root<Value>(gc::Pool&, gc::Root<Value>)>;

PromotionCallback GetImplicitPromotion(Type original, Type desired) {
  if (original == desired)
    return [](gc::Pool&, gc::Root<Value> value) { return value; };

  types::Function* original_function = std::get_if<types::Function>(&original);
  types::Function* desired_function = std::get_if<types::Function>(&desired);

  if (original_function != nullptr && desired_function != nullptr) {
    if (original_function->inputs.size() != desired_function->inputs.size())
      return nullptr;

    PromotionCallback output_callback = GetImplicitPromotion(
        original_function->output.get(), desired_function->output.get());
    if (output_callback == nullptr) return nullptr;

    std::vector<PromotionCallback> inputs_callbacks;
    for (size_t i = 0; i < original_function->inputs.size(); i++) {
      // Undo the promotion: we deliberately swap the order of desired and
      // original parameters for the function arguments.
      if (auto argument_callback = GetImplicitPromotion(
              desired_function->inputs[i], original_function->inputs[i]);
          argument_callback != nullptr) {
        inputs_callbacks.push_back(std::move(argument_callback));
      } else {
        return nullptr;
      }
    }

    return [output_callback, inputs_callbacks,
            purity = desired_function->function_purity](gc::Pool& pool,
                                                        gc::Root<Value> value) {
      const types::Function& value_function_type =
          std::get<types::Function>(value.ptr()->type);
      return Value::NewFunction(
          pool, purity, value_function_type.output.get(),
          value_function_type.inputs,
          std::bind_front(
              [output_callback, inputs_callbacks](
                  gc::Root<Value> original_callback,
                  std::vector<gc::Root<Value>> arguments,
                  Trampoline& trampoline) {
                CHECK_EQ(inputs_callbacks.size(), arguments.size());
                for (size_t i = 0; i < arguments.size(); ++i) {
                  arguments[i] = inputs_callbacks[i](trampoline.pool(),
                                                     std::move(arguments[i]));
                }
                return original_callback.ptr()
                    ->LockCallback()(std::move(arguments), trampoline)
                    .Transform([output_callback, &pool = trampoline.pool()](
                                   EvaluationOutput output) {
                      output.value =
                          output_callback(pool, std::move(output.value));
                      return Success(std::move(output));
                    });
              },
              std::move(value)));
    };
  }
  return nullptr;
}

namespace {
const bool tests_registration = tests::Register(
    L"GetImplicitPromotion",
    {
        {.name = L"NoPromotion",
         .callback =
             [] {
               CHECK(GetImplicitPromotion(types::String{}, types::Number{}) ==
                     nullptr);
             }},
        {.name = L"NumberToNumber",
         .callback =
             [] {
               gc::Pool pool({});
               PromotionCallback callback =
                   GetImplicitPromotion(types::Number{}, types::Number{});
               CHECK(callback != nullptr);
               gc::Root<Value> output =
                   callback(pool, Value::NewNumber(pool, Number(5)));
               ValueOrError<std::wstring> output_str =
                   ToString(output.ptr()->get_number(), 2);
               LOG(INFO) << "Output str: " << output_str;
               CHECK(ValueOrDie(output_str) == L"5");
             }},
        {.name = L"FunctionNoPromotion",
         .callback =
             [] {
               // No promotion: the return type doesn't match (int and string).
               std::vector<Type> inputs = {types::String(), types::Bool()};
               CHECK(GetImplicitPromotion(
                         types::Function{.output = Type{types::Number{}},
                                         .inputs = inputs},
                         types::Function{.output = Type{types::String{}},
                                         .inputs = inputs}) == nullptr);
             }},
        {.name = L"FunctionReturnType",
         .callback =
             [] {
               gc::Pool pool({});
               std::vector<Type> inputs = {types::String(), types::Bool()};
               gc::Root<Value> promoted_function = GetImplicitPromotion(
                   types::Function{.output = Type{types::Number{}},
                                   .inputs = inputs},
                   types::Function{.output = Type{types::Number{}},
                                   .inputs = inputs})(
                   pool, vm::NewCallback(pool, PurityType::kUnknown,
                                         [](std::wstring s, bool b) -> Number {
                                           CHECK(s == L"alejo");
                                           CHECK_EQ(b, true);
                                           return Number(4);
                                         }));
               Trampoline trampoline(Trampoline::Options{
                   .pool = pool,
                   .environment = Environment::NewDefault(pool),
                   .yield_callback = nullptr});
               futures::ValueOrError<EvaluationOutput> output =
                   promoted_function.ptr()->LockCallback()(
                       {Value::NewString(pool, L"alejo"),
                        Value::NewBool(pool, true)},
                       trampoline);
               CHECK(ValueOrDie(ToString(
                         std::get<EvaluationOutput>(output.Get().value())
                             .value.ptr()
                             ->get_number(),
                         2)) == L"4");
             }},
    });
}
}  // namespace afc::vm
