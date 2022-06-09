#ifndef __AFC_LANGUAGE_LAZY_STRING_TRIM_H__
#define __AFC_LANGUAGE_LAZY_STRING_TRIM_H__

#include <memory>
#include <string>

#include "src/language/lazy_string/lazy_string.h"
#include "src/language/safe_types.h"

namespace afc::language::lazy_string {
// Returns a copy with all left space characters removed.
language::NonNull<std::shared_ptr<LazyString>> StringTrimLeft(
    language::NonNull<std::shared_ptr<LazyString>> a,
    std::wstring space_characters);
}  // namespace afc::language::lazy_string

#endif  // __AFC_LANGUAGE_LAZY_STRING_TRIM_H__
