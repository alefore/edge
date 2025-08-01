#ifndef __AFC_VM_APPEND_EXPRESSION_H__
#define __AFC_VM_APPEND_EXPRESSION_H__

#include <memory>
#include <optional>

#include "src/language/error/value_or_error.h"
#include "src/language/gc.h"
#include "src/language/safe_types.h"

namespace afc::vm {
class Expression;
struct Compilation;

language::ValueOrError<language::gc::Root<Expression>> NewAppendExpression(
    Compilation& compilation, std::optional<language::gc::Ptr<Expression>> a,
    std::optional<language::gc::Ptr<Expression>> b);

language::ValueOrError<language::gc::Root<Expression>> NewAppendExpression(
    Compilation& compilation, std::unique_ptr<Expression> a,
    std::unique_ptr<Expression> b);

language::ValueOrError<language::gc::Root<Expression>> NewAppendExpression(
    language::gc::Ptr<Expression> a, language::gc::Ptr<Expression> b);

}  // namespace afc::vm

#endif  // __AFC_VM_APPEND_EXPRESSION_H__
