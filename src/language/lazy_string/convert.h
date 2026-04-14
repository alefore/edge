#ifndef __AFC_LANGUAGE_LAZY_STRING_CONVERT_H__
#define __AFC_LANGUAGE_LAZY_STRING_CONVERT_H__

#include <string>
#include <unordered_set>

#include "src/language/error/value_or_error.h"
#include "src/language/lazy_string/lazy_string.h"

namespace afc::language::lazy_string {
language::ValueOrError<int> AsInt(LazyString value);
}  // namespace afc::language::lazy_string

#endif  // __AFC_LANGUAGE_LAZY_STRING_CONVERT_H__