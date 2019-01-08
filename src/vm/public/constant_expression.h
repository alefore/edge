#ifndef __AFC_VM_CONSTANT_EXPRESSION_H__
#define __AFC_VM_CONSTANT_EXPRESSION_H__

#include <memory>

#include "src/vm/public/value.h"

namespace afc {
namespace vm {

using std::unique_ptr;

class Expression;
struct Value;

unique_ptr<Expression> NewVoidExpression();
unique_ptr<Expression> NewConstantExpression(Value::Ptr value);

}  // namespace vm
}  // namespace afc

#endif  // __AFC_VM_CONSTANT_EXPRESSION_H__
