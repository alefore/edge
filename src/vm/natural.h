#ifndef __AFC_VM_NATURAL_H__
#define __AFC_VM_NATURAL_H__

#include <vector>

#include "src/language/error/value_or_error.h"
#include "src/language/lazy_string/lazy_string.h"
#include "src/language/safe_types.h"
#include "src/vm/environment.h"
#include "src/vm/expression.h"

namespace afc::vm::natural {
// function_name_prefix will be prepended to the name of the top-level function.
// This can be used to select a `preview` function: the environment can define
// function `PreviewFoo` and `Foo` and we can select which one should be used.
language::ValueOrError<language::NonNull<std::shared_ptr<Expression>>> Compile(
    const language::lazy_string::SingleLine& input,
    const language::lazy_string::SingleLine& function_name_prefix,
    const Environment& environment,
    const std::vector<Namespace>& search_namespaces, language::gc::Pool& pool);
}  // namespace afc::vm::natural

#endif
