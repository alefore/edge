#ifndef __AFC_VM_CONSTANT_EXPRESSION_H__
#define __AFC_VM_CONSTANT_EXPRESSION_H__

#include <memory>

#include "src/language/gc.h"
#include "src/language/safe_types.h"

namespace afc::vm {
class Expression;
struct Value;

language::NonNull<std::unique_ptr<Expression>> NewVoidExpression(
    language::gc::Pool& pool);
language::NonNull<std::unique_ptr<Expression>> NewConstantExpression(
    language::gc::Root<Value> value);
}  // namespace afc::vm

#endif  // __AFC_VM_CONSTANT_EXPRESSION_H__
