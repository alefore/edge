#ifndef __AFC_VM_NEGATE_EXPRESSION_H__
#define __AFC_VM_NEGATE_EXPRESSION_H__

#include <memory>

#include "src/language/error/value_or_error.h"
#include "src/language/gc.h"

namespace afc::vm {
struct Compilation;
class Expression;

language::ValueOrError<language::gc::Root<Expression>> NewNegateExpressionBool(
    Compilation& compilation,
    language::ValueOrError<language::gc::Ptr<Expression>> expr);

language::ValueOrError<language::gc::Root<Expression>>
NewNegateExpressionNumber(
    Compilation& compilation,
    language::ValueOrError<language::gc::Ptr<Expression>> expr);
}  // namespace afc::vm

#endif  // __AFC_VM_NEGATE_EXPRESSION_H__
