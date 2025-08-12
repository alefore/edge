#ifndef __AFC_VM_CONSTANT_EXPRESSION_H__
#define __AFC_VM_CONSTANT_EXPRESSION_H__

#include <memory>

#include "src/language/gc.h"
#include "src/language/safe_types.h"

namespace afc::vm {
class Expression;
class Value;

language::gc::Root<Expression> NewVoidExpression(language::gc::Pool& pool);
language::gc::Root<Expression> NewConstantExpression(
    language::gc::Ptr<Value> value);
}  // namespace afc::vm

#endif  // __AFC_VM_CONSTANT_EXPRESSION_H__
