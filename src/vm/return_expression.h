#ifndef __AFC_VM_RETURN_EXPRESSION_H__
#define __AFC_VM_RETURN_EXPRESSION_H__

#include "src/language/error/value_or_error.h"
#include "src/vm/compilation.h"
#include "src/vm/expression.h"

namespace afc::vm {
language::ValueOrError<language::gc::Root<Expression>> NewReturnExpression(
    language::ValueOrError<language::gc::Ptr<Expression>> expr);
}  // namespace afc::vm

#endif  // __AFC_VM_RETURN_EXPRESSION_H__
