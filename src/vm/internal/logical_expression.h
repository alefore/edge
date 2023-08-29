#ifndef __AFC_VM_LOGICAL_EXPRESSION_H__
#define __AFC_VM_LOGICAL_EXPRESSION_H__

#include <memory>

#include "src/language/error/value_or_error.h"
#include "src/language/safe_types.h"

namespace afc::vm {

class Expression;
struct Compilation;

language::ValueOrError<language::NonNull<std::unique_ptr<Expression>>>
NewLogicalExpression(Compilation* compilation, bool identity,
                     std::unique_ptr<Expression> a,
                     std::unique_ptr<Expression> b);

}  // namespace afc::vm

#endif  // __AFC_VM_LOGICAL_EXPRESSION_H__
