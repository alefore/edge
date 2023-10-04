#ifndef __AFC_VM_RETURN_EXPRESSION_H__
#define __AFC_VM_RETURN_EXPRESSION_H__

#include "src/vm/compilation.h"
#include "src/vm/expression.h"

namespace afc::vm {
std::unique_ptr<Expression> NewReturnExpression(
    Compilation* compilation, std::unique_ptr<Expression> expr);
}  // namespace afc::vm

#endif  // __AFC_VM_RETURN_EXPRESSION_H__
