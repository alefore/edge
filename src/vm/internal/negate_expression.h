#ifndef __AFC_VM_NEGATE_EXPRESSION_H__
#define __AFC_VM_NEGATE_EXPRESSION_H__

#include <functional>
#include <memory>

namespace afc {
namespace vm {

using std::unique_ptr;

class Compilation;
class Expression;
class Value;
class VMType;

unique_ptr<Expression> NewNegateExpression(
    std::function<void(Value*)> negate,
    const VMType& expected_type,
    Compilation* compilation,
    unique_ptr<Expression> expr);

}  // namespace vm
}  // namespace afc

#endif  // __AFC_VM_NEGATE_EXPRESSION_H__
