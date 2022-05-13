#ifndef __AFC_VM_NEGATE_EXPRESSION_H__
#define __AFC_VM_NEGATE_EXPRESSION_H__

#include <memory>

namespace afc::vm {
class Compilation;
class Expression;

std::unique_ptr<Expression> NewNegateExpressionBool(
    Compilation& compilation, std::unique_ptr<Expression> expr);
std::unique_ptr<Expression> NewNegateExpressionInt(
    Compilation& compilation, std::unique_ptr<Expression> expr);
std::unique_ptr<Expression> NewNegateExpressionDouble(
    Compilation& compilation, std::unique_ptr<Expression> expr);

}  // namespace afc::vm

#endif  // __AFC_VM_NEGATE_EXPRESSION_H__
