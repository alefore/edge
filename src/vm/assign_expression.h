#ifndef __AFC_VM_ASSIGN_EXPRESSION_H__
#define __AFC_VM_ASSIGN_EXPRESSION_H__

#include <memory>
#include <optional>
#include <string>

#include "src/language/error/value_or_error.h"
#include "src/language/gc.h"
#include "src/vm/types.h"

namespace afc::vm {

struct Compilation;
class Expression;
class Environment;

// Declares a new variable of a given type.
language::ValueOrError<Type> DefineUninitializedVariable(
    Environment&, Identifier type, Identifier symbol,
    std::optional<Type> default_type);

// Declares a new variable of a given type and gives it an initial value.
language::ValueOrError<language::gc::Root<Expression>> NewDefineExpression(
    Compilation& compilation, Identifier type, Identifier symbol,
    language::ValueOrError<language::gc::Ptr<Expression>> value);

// Returns an expression that assigns a given value to an existing variable.
language::ValueOrError<language::gc::Root<Expression>> NewAssignExpression(
    Compilation& compilation, Identifier symbol,
    language::ValueOrError<language::gc::Ptr<Expression>> value);

}  // namespace afc::vm

#endif  // __AFC_VM_ASSIGN_EXPRESSION_H__
