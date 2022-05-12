#include "src/vm/internal/types_promotion.h"

namespace afc::vm {
using language::NonNull;
using language::Success;

namespace gc = language::gc;

using PromotionCallback =
    std::function<gc::Root<Value>(gc::Pool&, gc::Root<Value>)>;

PromotionCallback GetImplicitPromotion(VMType original, VMType desired) {
  if (original == desired)
    return [](gc::Pool&, gc::Root<Value> value) { return value; };
  switch (original.type) {
    case VMType::VM_INTEGER:
      switch (desired.type) {
        case VMType::VM_DOUBLE:
          return [](gc::Pool& pool, gc::Root<Value> value) {
            CHECK(value.value()->IsInteger());
            return Value::NewDouble(pool, value.value()->integer);
          };
        default:
          return nullptr;
      }
      return nullptr;

    case VMType::FUNCTION:
      switch (desired.type) {
        case VMType::FUNCTION: {
          if (original.type_arguments.size() != desired.type_arguments.size())
            return nullptr;
          std::vector<PromotionCallback> argument_callbacks;
          for (size_t i = 0; i < original.type_arguments.size(); i++) {
            // Undo the promotion: we deliberately swap the order of desired and
            // original parameters for the function arguments.
            if (auto argument_callback =
                    i == 0 ? GetImplicitPromotion(original.type_arguments[i],
                                                  desired.type_arguments[i])
                           : GetImplicitPromotion(desired.type_arguments[i],
                                                  original.type_arguments[i]);
                argument_callback != nullptr) {
              argument_callbacks.push_back(std::move(argument_callback));
            } else {
              return nullptr;
            }
          }

          return [argument_callbacks, purity = desired.function_purity](
                     gc::Pool& pool, gc::Root<Value> value) {
            std::vector<VMType> type_arguments =
                value.value()->type.type_arguments;
            gc::Root<Value> output = Value::NewFunction(
                pool, type_arguments,
                std::bind_front(
                    [argument_callbacks](gc::Root<Value> original_callback,
                                         std::vector<gc::Root<Value>> arguments,
                                         Trampoline& trampoline) {
                      CHECK_EQ(argument_callbacks.size(), arguments.size() + 1);
                      for (size_t i = 0; i < arguments.size(); ++i) {
                        arguments[i] = argument_callbacks[i + 1](
                            trampoline.pool(), std::move(arguments[i]));
                      }
                      return original_callback.value()
                          ->LockCallback()(std::move(arguments), trampoline)
                          .Transform([return_callback = argument_callbacks[0],
                                      &pool = trampoline.pool()](
                                         EvaluationOutput output) {
                            output.value =
                                return_callback(pool, std::move(output.value));
                            return Success(std::move(output));
                          });
                    },
                    std::move(value)));
            output.value()->type.function_purity = purity;
            return output;
          };
        }

        default:
          return nullptr;
      }

    default:
      return nullptr;
  }
}
}  // namespace afc::vm
