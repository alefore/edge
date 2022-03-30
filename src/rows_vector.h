#ifndef __AFC_EDITOR_ROWS_VECTOR_H__
#define __AFC_EDITOR_ROWS_VECTOR_H__

#include <memory>
#include <vector>

#include "src/buffer.h"
#include "src/line_with_cursor.h"

namespace afc {
namespace editor {

struct RowsVector {
 public:
  LineWithCursor::Generator::Vector& back() {
    CHECK(!rows.empty());
    return rows.back();
  }
  void push_back(LineWithCursor::Generator::Vector row) {
    rows.push_back(std::move(row));
  }

  std::vector<LineWithCursor::Generator::Vector> rows = {};
  size_t index_active = 0;
  LineNumberDelta lines;
};

LineWithCursor::Generator::Vector OutputFromRowsVector(RowsVector table);

LineWithCursor::Generator::Vector AppendRows(
    LineWithCursor::Generator::Vector head,
    LineWithCursor::Generator::Vector tail, size_t index_active);
}  // namespace editor
}  // namespace afc

#endif  // __AFC_EDITOR_ROWS_VECTOR_H__
