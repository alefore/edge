#ifndef __AFC_VM_DELEGATING_EXPRESSION_H__
#define __AFC_VM_DELEGATING_EXPRESSION_H__

#include <memory>

#include "src/language/error/value_or_error.h"
#include "src/language/gc.h"
#include "src/language/safe_types.h"
#include "src/vm/expression.h"

namespace afc::vm {
// Factory function to create a NonNull<std::unique_ptr<Expression>> from a
// gc::Root<Expression>. This bridges the unique_ptr (for cpp.y and similar
// consumers) and gc::Root worlds.
language::NonNull<std::unique_ptr<Expression>> NewDelegatingExpression(
    language::gc::Root<Expression> delegate);
}  // namespace afc::vm

#endif  // __AFC_VM_DELEGATING_EXPRESSION_H__
