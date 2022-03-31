#ifndef __AFC_EDITOR_ROWS_VECTOR_H__
#define __AFC_EDITOR_ROWS_VECTOR_H__

#include <memory>
#include <vector>

#include "src/buffer.h"
#include "src/line_with_cursor.h"

namespace afc::editor {

// Complexity is linear to the length of `tail`.
LineWithCursor::Generator::Vector AppendRows(
    LineWithCursor::Generator::Vector head,
    LineWithCursor::Generator::Vector tail, size_t index_active);

}  // namespace afc::editor

#endif  // __AFC_EDITOR_ROWS_VECTOR_H__
