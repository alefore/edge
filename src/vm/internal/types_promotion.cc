#include "src/vm/internal/types_promotion.h"

namespace afc::vm {
using PromotionCallback =
    std::function<std::unique_ptr<Value>(std::unique_ptr<Value>)>;

PromotionCallback GetImplicitPromotion(VMType original, VMType desired) {
  if (original == desired)
    return [](std::unique_ptr<Value> value) { return value; };
  switch (original.type) {
    case VMType::VM_INTEGER:
      switch (desired.type) {
        case VMType::VM_DOUBLE:
          return [](std::unique_ptr<Value> value) {
            return Value::NewDouble(value->integer);
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
            // original parameters.
            if (auto argument_callback = GetImplicitPromotion(
                    desired.type_arguments[i], original.type_arguments[i]);
                argument_callback != nullptr) {
              argument_callbacks.push_back(std::move(argument_callback));
            } else {
              return nullptr;
            }
          }

          return [argument_callbacks, purity = desired.function_purity](
                     std::unique_ptr<Value> value) {
            value->type.function_purity = purity;
            value->callback = [argument_callbacks,
                               original_callback = std::move(value->callback)](
                                  std::vector<std::unique_ptr<Value>> arguments,
                                  Trampoline* trampoline) {
              CHECK_EQ(argument_callbacks.size(), arguments.size() + 1);
              for (size_t i = 0; i < arguments.size(); ++i) {
                arguments[i] =
                    argument_callbacks[i + 1](std::move(arguments[i]));
              }
              return original_callback(std::move(arguments), trampoline)
                  .Transform([return_callback = argument_callbacks[0]](
                                 EvaluationOutput output) {
                    if (output.value != nullptr) {
                      output.value = return_callback(std::move(output.value));
                    }
                    return output;
                  });
            };
            return value;
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
