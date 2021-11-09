#ifndef __AFC_VM_PUBLIC_TYPES_PROMOTION_H__
#define __AFC_VM_PUBLIC_TYPES_PROMOTION_H__

#include <functional>
#include <memory>

#include "src/vm/public/types.h"
#include "src/vm/public/value.h"

namespace afc::vm {
// If a value of `original` type can be promoted implicitly to a value of
// `desired` type, returns a function that executes the promotion.
std::function<std::unique_ptr<Value>(std::unique_ptr<Value>)>
GetImplicitPromotion(VMType original, VMType desired);
}  // namespace afc::vm

#endif
