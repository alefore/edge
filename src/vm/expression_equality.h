#ifndef __AFC_VM_EXPRESSION_EQUALITY_H__
#define __AFC_VM_EXPRESSION_EQUALITY_H__

#include <memory>

#include "src/language/gc.h"

namespace afc::vm {
struct Compilation;
class Expression;

language::ValueOrError<language::gc::Root<Expression>> ExpressionEquals(
    Compilation& compilation,
    language::ValueOrError<language::gc::Ptr<Expression>> a,
    language::ValueOrError<language::gc::Ptr<Expression>> b);

}  // namespace afc::vm

#endif  // __AFC_VM_EXPRESSION_EQUALITY_H__