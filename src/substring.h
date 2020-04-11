#ifndef __AFC_EDITOR_SUBSTRING_H__
#define __AFC_EDITOR_SUBSTRING_H__

#include <memory>

#include "lazy_string.h"
#include "line_column.h"

namespace afc {
namespace editor {

using std::shared_ptr;

// Returns the substring from pos to the end of the string.
//
// Equivalent to:
//
//     Substring(input, pos, input.size() - pos);
std::shared_ptr<LazyString> Substring(std::shared_ptr<LazyString> input,
                                      ColumnNumber column);

// Returns the contents in [pos, pos + len).
//
// pos and len must be in the correct range (or else we'll crash).
//
// Example: Substring("alejo", 1, 2) := "le"
std::shared_ptr<LazyString> Substring(std::shared_ptr<LazyString> input,
                                      ColumnNumber column,
                                      ColumnNumberDelta delta);

// Similar to the other versions, but performs checks on the bounds; instead of
// crashing on invalid bounds, returns a shorter string.
//
// Example: Substring("carla", 2, 30) := "rla"
std::shared_ptr<LazyString> SubstringWithRangeChecks(
    shared_ptr<LazyString> input, ColumnNumber column, ColumnNumberDelta delta);

}  // namespace editor
}  // namespace afc

#endif  // __AFC_EDITOR_SUBSTRING_H__
