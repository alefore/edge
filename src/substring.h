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
//
// TODO: Remove the versions that take `size_t` after migrating to ColumnNumber.
shared_ptr<LazyString> Substring(const shared_ptr<LazyString>& input,
                                 size_t pos);
shared_ptr<LazyString> Substring(const shared_ptr<LazyString>& input,
                                 ColumnNumber column);

// Returns the contents in [pos, pos + len).
//
// Example: Substring("alejo", 1, 2) := "le"
shared_ptr<LazyString> Substring(const shared_ptr<LazyString>& input,
                                 size_t pos, size_t size);
shared_ptr<LazyString> Substring(const shared_ptr<LazyString>& input,
                                 ColumnNumber column, ColumnNumberDelta delta);

}  // namespace editor
}  // namespace afc

#endif  // __AFC_EDITOR_SUBSTRING_H__
