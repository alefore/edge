#ifndef __AFC_LANGAUGE_LAZY_STRING_SUBSTRING_H__
#define __AFC_LANGAUGE_LAZY_STRING_SUBSTRING_H__

#include <memory>

#include "src/language/lazy_string/lazy_string.h"
#include "src/language/safe_types.h"

namespace afc::language::lazy_string {
// Returns the substring from pos to the end of the string.
//
// Equivalent to:
//
//     Substring(input, pos, input.size() - pos);
LazyString Substring(LazyString input, ColumnNumber column);

// Returns the contents in [pos, pos + len).
//
// pos and len must be in the correct range (or else we'll crash).
//
// Example: Substring("alejo", 1, 2) := "le"
LazyString Substring(LazyString input, ColumnNumber column,
                     ColumnNumberDelta delta);

// Similar to the other versions, but performs checks on the bounds; instead of
// crashing on invalid bounds, returns a shorter string.
//
// Example: Substring("carla", 2, 30) := "rla"
LazyString SubstringWithRangeChecks(LazyString input, ColumnNumber column,
                                    ColumnNumberDelta delta);
}  // namespace afc::language::lazy_string
#endif  // __AFC_LANGAUGE_LAZY_STRING_SUBSTRING_H__
