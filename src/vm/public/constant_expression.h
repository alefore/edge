#ifndef __AFC_VM_CONSTANT_EXPRESSION_H__
#define __AFC_VM_CONSTANT_EXPRESSION_H__

#include <memory>

#include "src/language/safe_types.h"
#include "src/vm/public/value.h"

namespace afc::vm {
class Expression;
struct Value;

language::NonNull<std::unique_ptr<Expression>> NewVoidExpression();
language::NonNull<std::unique_ptr<Expression>> NewConstantExpression(
    language::NonNull<Value::Ptr> value);
}  // namespace afc::vm

#endif  // __AFC_VM_CONSTANT_EXPRESSION_H__
