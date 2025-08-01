#ifndef __AFC_VM_NEGATE_EXPRESSION_H__
#define __AFC_VM_NEGATE_EXPRESSION_H__

#include <memory>

#include "src/language/error/value_or_error.h"
#include "src/language/gc.h"

namespace afc::vm {
struct Compilation;
class Expression;

// TODO(trivial, 2025-08-01): Receive `expr` as `std::optional<gc::Ptr<>>`
language::ValueOrError<language::gc::Root<Expression>> NewNegateExpressionBool(
    Compilation& compilation,
    std::optional<language::gc::Root<Expression>> expr);

// TODO(trivial, 2025-08-01): Receive `expr` as `std::optional<gc::Ptr<>>`
language::ValueOrError<language::gc::Root<Expression>>
NewNegateExpressionNumber(Compilation& compilation,
                          std::optional<language::gc::Root<Expression>> expr);
}  // namespace afc::vm

#endif  // __AFC_VM_NEGATE_EXPRESSION_H__
