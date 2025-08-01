#ifndef __AFC_VM_INTERNAL_NAMESPACE_EXPRESSION_H__
#define __AFC_VM_INTERNAL_NAMESPACE_EXPRESSION_H__

#include <memory>

#include "src/language/error/value_or_error.h"
#include "src/language/gc.h"
#include "src/vm/expression.h"

namespace afc::vm {
struct Compilation;
void StartNamespaceDeclaration(Compilation& compilation,
                               const Identifier& name);

language::ValueOrError<language::gc::Root<Expression>> NewNamespaceExpression(
    Compilation& compilation,
    std::optional<language::gc::Root<Expression>> body);
}  // namespace afc::vm

#endif  // __AFC_VM_INTERNAL_NAMESPACE_EXPRESSION_H__
