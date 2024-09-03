#ifndef __AFC_LANGUAGE_LAZY_STRING_TRIM_H__
#define __AFC_LANGUAGE_LAZY_STRING_TRIM_H__

#include <string>
#include <unordered_set>

#include "src/language/lazy_string/lazy_string.h"
#include "src/language/safe_types.h"

namespace afc::language::lazy_string {
// Returns a copy with all left space characters removed.
LazyString TrimLeft(LazyString a, std::wstring space_characters);
LazyString Trim(LazyString a, std::unordered_set<wchar_t> space_characters);
}  // namespace afc::language::lazy_string

#endif  // __AFC_LANGUAGE_LAZY_STRING_TRIM_H__
