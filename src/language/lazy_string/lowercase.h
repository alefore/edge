// TODO(trivial, 2024-08-28): Move to `case.h`, since this can also conver to
// upper case.
#ifndef __AFC_LANGUAGE_LAZY_STRING_LOWERCASE_H__
#define __AFC_LANGUAGE_LAZY_STRING_LOWERCASE_H__

#include <memory>

#include "src/language/lazy_string/lazy_string.h"
#include "src/language/lazy_string/single_line.h"
#include "src/language/safe_types.h"

namespace afc::language::lazy_string {
LazyString LowerCase(LazyString input);
LazyString UpperCase(LazyString input);

SingleLine LowerCase(SingleLine input);
SingleLine UpperCase(SingleLine input);

NonEmptySingleLine LowerCase(NonEmptySingleLine input);
NonEmptySingleLine UpperCase(NonEmptySingleLine input);
}  // namespace afc::language::lazy_string

#endif  // __AFC_LANGUAGE_LAZY_STRING_LOWERCASE_H__
