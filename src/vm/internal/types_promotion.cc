#include "src/vm/internal/types_promotion.h"

namespace afc::vm {
using language::NonNull;
using language::Success;

namespace gc = language::gc;

using PromotionCallback =
    std::function<gc::Root<Value>(gc::Pool&, gc::Root<Value>)>;

PromotionCallback GetImplicitPromotion(Type original, Type desired) {
  if (original == desired)
    return [](gc::Pool&, gc::Root<Value> value) { return value; };
  if (std::holds_alternative<types::Int>(original) &&
      std::holds_alternative<types::Double>(desired)) {
    return [](gc::Pool& pool, gc::Root<Value> value) {
      return Value::NewDouble(pool, value.ptr()->get_int());
    };
  }

  types::Function* original_function = std::get_if<types::Function>(&original);
  types::Function* desired_function = std::get_if<types::Function>(&desired);

  if (original_function != nullptr && desired_function != nullptr) {
    if (original_function->inputs.size() != desired_function->inputs.size())
      return nullptr;

    PromotionCallback output_callback = GetImplicitPromotion(
        original_function->output.get(), desired_function->output.get());

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
}  // namespace afc::vm
