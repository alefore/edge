#include "src/vm/internal/types_promotion.h"

namespace afc::vm {
std::function<std::unique_ptr<Value>(std::unique_ptr<Value>)>
GetImplicitPromotion(VMType original, VMType desired) {
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
    case VMType::VM_DOUBLE:
      switch (desired.type) {
        case VMType::VM_INTEGER:
          return [](std::unique_ptr<Value> value) {
            return Value::NewInteger(value->double_value);
          };
        default:
          return nullptr;
      }
    default:
      return nullptr;
  }
}
}  // namespace afc::vm
