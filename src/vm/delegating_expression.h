#ifndef __AFC_VM_DELEGATING_EXPRESSION_H__
#define __AFC_VM_DELEGATING_EXPRESSION_H__

#include <memory>

#include "src/language/error/value_or_error.h"
#include "src/language/gc.h"
#include "src/language/safe_types.h"
#include "src/vm/expression.h"

namespace afc::vm {
std::optional<language::gc::Root<Expression>> PtrToOptionalRoot(
    language::gc::Pool& pool, std::unique_ptr<Expression> expr);

// Factory function to create a NonNull<std::unique_ptr<Expression>> from a
// gc::Root<Expression>. This bridges the unique_ptr (for cpp.y and similar
// consumers) and gc::Root worlds.
language::NonNull<std::unique_ptr<Expression>> NewDelegatingExpression(
    language::gc::Root<Expression> delegate);

std::optional<language::gc::Ptr<Expression>> OptionalRootToPtr(
    const std::optional<language::gc::Root<Expression>>&);

template <typename T>
std::optional<language::gc::Root<T>> MoveOutAndDelete(
    std::optional<language::gc::Root<T>>* value_raw) {
  if (std::unique_ptr<std::optional<language::gc::Root<T>>> value{value_raw};
      value != nullptr)
    return std::move(*value);
  return std::nullopt;
}

}  // namespace afc::vm

#endif  // __AFC_VM_DELEGATING_EXPRESSION_H__
