#ifndef __AFC_VM_CONSTANT_EXPRESSION_H__
#define __AFC_VM_CONSTANT_EXPRESSION_H__

#include <memory>

namespace afc {
namespace vm {

using std::unique_ptr;

class Expression;
class Value;

unique_ptr<Expression> NewVoidExpression();
unique_ptr<Expression> NewConstantExpression(unique_ptr<Value> value);

}  // namespace vm
}  // namespace afc

#endif  // __AFC_VM_CONSTANT_EXPRESSION_H__
