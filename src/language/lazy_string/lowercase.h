#ifndef __AFC_LANGUAGE_LAZY_STRING_LOWERCASE_H__
#define __AFC_LANGUAGE_LAZY_STRING_LOWERCASE_H__

#include <memory>

#include "src/language/lazy_string/lazy_string.h"
#include "src/language/safe_types.h"

namespace afc::language::lazy_string {
LazyString LowerCase(LazyString input);
}  // namespace afc::language::lazy_string

#endif  // __AFC_LANGUAGE_LAZY_STRING_LOWERCASE_H__
