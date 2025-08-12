#ifndef __AFC_VM_RETURN_EXPRESSION_H__
#define __AFC_VM_RETURN_EXPRESSION_H__

#include "src/language/error/value_or_error.h"
#include "src/vm/compilation.h"
#include "src/vm/expression.h"

namespace afc::vm {
std::optional<language::gc::Root<Expression>> NewReturnExpression(
    std::optional<language::gc::Ptr<Expression>> expr);
}  // namespace afc::vm

#endif  // __AFC_VM_RETURN_EXPRESSION_H__
