#ifndef __AFC_VM_INTERNAL_IF_EXPRESSION_H__
#define __AFC_VM_INTERNAL_IF_EXPRESSION_H__

#include <memory>

#include "src/language/error/value_or_error.h"
#include "src/language/gc.h"
#include "src/language/safe_types.h"
#include "src/vm/expression.h"

namespace afc::vm {
struct Compilation;
language::ValueOrError<language::gc::Root<Expression>> NewIfExpression(
    Compilation& compilation,
    language::ValueOrError<language::gc::Ptr<Expression>> condition,
    language::ValueOrError<language::gc::Ptr<Expression>> true_case,
    language::ValueOrError<language::gc::Ptr<Expression>> false_case);
}  // namespace afc::vm

#endif  // __AFC_VM_INTERNAL_IF_EXPRESSION_H__
