#ifndef __AFC_VM_NEGATE_EXPRESSION_H__
#define __AFC_VM_NEGATE_EXPRESSION_H__

#include <memory>

namespace afc::vm {
struct Compilation;
class Expression;

std::unique_ptr<Expression> NewNegateExpressionBool(
    Compilation& compilation, std::unique_ptr<Expression> expr);
std::unique_ptr<Expression> NewNegateExpressionNumber(
    Compilation& compilation, std::unique_ptr<Expression> expr);
}  // namespace afc::vm

#endif  // __AFC_VM_NEGATE_EXPRESSION_H__
