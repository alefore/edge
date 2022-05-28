#ifndef __AFC_VM_INTERNAL_IF_EXPRESSION_H__
#define __AFC_VM_INTERNAL_IF_EXPRESSION_H__

#include <memory>

#include "../public/vm.h"
#include "src/language/safe_types.h"
#include "src/language/value_or_error.h"

namespace afc::vm {
class Compilation;
language::ValueOrError<language::NonNull<std::unique_ptr<Expression>>>
NewIfExpression(Compilation* compilation, std::unique_ptr<Expression> condition,
                std::unique_ptr<Expression> true_case,
                std::unique_ptr<Expression> false_case);
}  // namespace afc::vm

#endif  // __AFC_VM_INTERNAL_IF_EXPRESSION_H__
