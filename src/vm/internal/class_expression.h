#ifndef __AFC_VM_INTERNAL_CLASS_EXPRESSION_H__
#define __AFC_VM_INTERNAL_CLASS_EXPRESSION_H__

#include <memory>

#include "../public/vm.h"
#include "src/language/safe_types.h"
#include "src/language/error/value_or_error.h"

namespace afc::vm {
class Compilation;
namespace types {
class ObjectName;
}
void StartClassDeclaration(Compilation& compilation,
                           const types::ObjectName& name);
language::PossibleError FinishClassDeclaration(
    Compilation& compilation,
    language::NonNull<std::unique_ptr<Expression>> body);
}  // namespace afc::vm

#endif  // __AFC_VM_INTERNAL_CLASS_EXPRESSION_H__
