#ifndef __AFC_VM_INTERNAL_IF_EXPRESSION_H__
#define __AFC_VM_INTERNAL_IF_EXPRESSION_H__

#include <memory>

#include "src/language/error/value_or_error.h"
#include "src/language/gc.h"
#include "src/language/safe_types.h"
#include "src/vm/expression.h"

namespace afc::vm {
struct Compilation;
// TODO(trivial, 2025-08-01): Parameters should be gc::Ptr.
language::ValueOrError<language::gc::Root<Expression>> NewIfExpression(
    Compilation& compilation,
    std::optional<language::gc::Root<Expression>> condition,
    std::optional<language::gc::Root<Expression>> true_case,
    std::optional<language::gc::Root<Expression>> false_case);
}  // namespace afc::vm

#endif  // __AFC_VM_INTERNAL_IF_EXPRESSION_H__
