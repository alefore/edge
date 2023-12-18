#ifndef __AFC_VM_NATURAL_H__
#define __AFC_VM_NATURAL_H__

#include <vector>

#include "src/language/error/value_or_error.h"
#include "src/language/lazy_string/lazy_string.h"
#include "src/language/safe_types.h"
#include "src/vm/expression.h"

namespace afc::vm::natural {
language::ValueOrError<language::NonNull<std::shared_ptr<Expression>>> Compile(
    const language::lazy_string::LazyString& input,
    const Environment& environment, language::gc::Pool& pool);
}  // namespace afc::vm::natural

#endif
