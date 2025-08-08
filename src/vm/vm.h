#ifndef __AFC_VM_PUBLIC_VM_H__
#define __AFC_VM_PUBLIC_VM_H__

#include <memory>
#include <utility>

#include "src/infrastructure/dirname.h"
#include "src/language/error/value_or_error.h"
#include "src/language/gc.h"
#include "src/language/lazy_string/lazy_string.h"
#include "src/language/safe_types.h"
#include "src/vm/environment.h"
#include "src/vm/expression.h"
#include "src/vm/types.h"

namespace afc::vm {
language::ValueOrError<language::gc::Root<Expression>> CompileFile(
    infrastructure::Path path, language::gc::Pool& pool,
    language::gc::Root<Environment> environment);

language::ValueOrError<language::gc::Root<Expression>> CompileString(
    const language::lazy_string::LazyString& str, language::gc::Pool& pool,
    language::gc::Root<Environment> environment);
}  // namespace afc::vm

#endif  // __AFC_VM_VM_H__
