#ifndef __AFC_VM_WHILE_EXPRESSION_H__
#define __AFC_VM_WHILE_EXPRESSION_H__

#include <memory>

#include "src/language/error/value_or_error.h"
#include "src/language/gc.h"
#include "src/language/safe_types.h"

namespace afc::vm {

struct Compilation;
class Expression;

language::ValueOrError<language::gc::Root<Expression>> NewWhileExpression(
    Compilation& compilation,
    language::ValueOrError<language::gc::Ptr<Expression>> cond,
    language::ValueOrError<language::gc::Ptr<Expression>> body);

language::ValueOrError<language::gc::Root<Expression>> NewForExpression(
    Compilation& compilation,
    language::ValueOrError<language::gc::Ptr<Expression>> init,
    language::ValueOrError<language::gc::Ptr<Expression>> condition,
    language::ValueOrError<language::gc::Ptr<Expression>> update,
    language::ValueOrError<language::gc::Ptr<Expression>> body);

}  // namespace afc::vm

#endif  // __AFC_VM_WHILE_EXPRESSION_H__
