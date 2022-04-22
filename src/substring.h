#ifndef __AFC_EDITOR_SUBSTRING_H__
#define __AFC_EDITOR_SUBSTRING_H__

#include <memory>

#include "lazy_string.h"
#include "line_column.h"
#include "src/language/safe_types.h"

namespace afc::editor {
// Returns the substring from pos to the end of the string.
//
// Equivalent to:
//
//     Substring(input, pos, input.size() - pos);
language::NonNull<std::shared_ptr<LazyString>> Substring(
    language::NonNull<std::shared_ptr<LazyString>> input, ColumnNumber column);

// Returns the contents in [pos, pos + len).
//
// pos and len must be in the correct range (or else we'll crash).
//
// Example: Substring("alejo", 1, 2) := "le"
language::NonNull<std::shared_ptr<LazyString>> Substring(
    language::NonNull<std::shared_ptr<LazyString>> input, ColumnNumber column,
    ColumnNumberDelta delta);

// Similar to the other versions, but performs checks on the bounds; instead of
// crashing on invalid bounds, returns a shorter string.
//
// Example: Substring("carla", 2, 30) := "rla"
language::NonNull<std::shared_ptr<LazyString>> SubstringWithRangeChecks(
    language::NonNull<std::shared_ptr<LazyString>> input, ColumnNumber column,
    ColumnNumberDelta delta);
}  // namespace afc::editor
#endif  // __AFC_EDITOR_SUBSTRING_H__
