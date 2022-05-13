#ifndef __AFC_VM_INTERNAL_CLASS_EXPRESSION_H__
#define __AFC_VM_INTERNAL_CLASS_EXPRESSION_H__

#include <memory>

#include "../public/vm.h"
#include "src/language/safe_types.h"

namespace afc::vm {
class Compilation;
void StartClassDeclaration(Compilation& compilation, const std::wstring& name);
void FinishClassDeclaration(
    Compilation& compilation,
    language::NonNull<std::unique_ptr<Expression>> body);
}  // namespace afc::vm

#endif  // __AFC_VM_INTERNAL_CLASS_EXPRESSION_H__
