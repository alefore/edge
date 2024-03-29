#ifndef __AFC_VM_INTERNAL_NAMESPACE_EXPRESSION_H__
#define __AFC_VM_INTERNAL_NAMESPACE_EXPRESSION_H__

#include <memory>

#include "src/vm/expression.h"

namespace afc::vm {
struct Compilation;
void StartNamespaceDeclaration(Compilation& compilation,
                               const Identifier& name);

std::unique_ptr<Expression> NewNamespaceExpression(
    Compilation& compilation, std::unique_ptr<Expression> body);
}  // namespace afc::vm

#endif  // __AFC_VM_INTERNAL_NAMESPACE_EXPRESSION_H__
