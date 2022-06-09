#ifndef __AFC_LANGUAGE_LAZY_STRING_PADDING_H__
#define __AFC_LANGUAGE_LAZY_STRING_PADDING_H__

#include <memory>
#include <string>

#include "src/language/lazy_string/lazy_string.h"
#include "src/language/safe_types.h"

namespace afc::language::lazy_string {
// Generates a string of the length specified by `this` filled up with the
// character given.
//
// If length is negative (or zero), returns an empty string.
language::NonNull<std::shared_ptr<afc::language::lazy_string::LazyString>>
Padding(const afc::language::lazy_string::ColumnNumberDelta& length,
        wchar_t fill);
}  // namespace afc::language::lazy_string

#endif  // __AFC_LANGUAGE_LAZY_STRING_PADDING_H__
