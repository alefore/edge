#ifndef __AFC_VM_CONSTANT_EXPRESSION_H__
#define __AFC_VM_CONSTANT_EXPRESSION_H__

#include <memory>

#include "src/language/safe_types.h"
#include "src/vm/public/value.h"

namespace afc {
namespace vm {

class Expression;
struct Value;

std::unique_ptr<Expression> NewVoidExpression();
std::unique_ptr<Expression> NewConstantExpression(
    language::NonNull<Value::Ptr> value);

}  // namespace vm
}  // namespace afc

#endif  // __AFC_VM_CONSTANT_EXPRESSION_H__
