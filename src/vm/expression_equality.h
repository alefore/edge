#ifndef __AFC_VM_EXPRESSION_EQUALITY_H__
#define __AFC_VM_EXPRESSION_EQUALITY_H__

#include <memory>

namespace afc::vm {
struct Compilation;
class Expression;

std::unique_ptr<Expression> ExpressionEquals(
    Compilation& compilation,
    std::unique_ptr<Expression> a,
    std::unique_ptr<Expression> b);

}  // namespace afc::vm

#endif  // __AFC_VM_EXPRESSION_EQUALITY_H__