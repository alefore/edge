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
    std::optional<language::gc::Root<Expression>> cond,
    std::optional<language::gc::Root<Expression>> body);

language::ValueOrError<language::gc::Root<Expression>> NewForExpression(
    Compilation& compilation,
    std::optional<language::gc::Root<Expression>> init,
    std::optional<language::gc::Root<Expression>> condition,
    std::optional<language::gc::Root<Expression>> update,
    std::optional<language::gc::Root<Expression>> body);

}  // namespace afc::vm

#endif  // __AFC_VM_WHILE_EXPRESSION_H__
