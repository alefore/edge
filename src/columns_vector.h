#ifndef __AFC_EDITOR_COLUMNS_VECTOR_H__
#define __AFC_EDITOR_COLUMNS_VECTOR_H__

#include <memory>
#include <vector>

#include "src/buffer.h"
#include "src/line_with_cursor.h"

namespace afc::editor {

struct ColumnsVector {
  struct Column {
    LineWithCursor::Generator::Vector lines;

    // If absent, this column will be the last column produced, and it will be
    // allowed to span the entire screen.
    std::optional<ColumnNumberDelta> width = std::nullopt;
  };

  Column& back() {
    CHECK(!columns.empty());
    return columns.back();
  }
  void push_back(Column column) { columns.push_back(std::move(column)); }

  std::vector<Column> columns = {};
  size_t index_active = 0;
  LineNumberDelta lines;
};

LineWithCursor::Generator::Vector OutputFromColumnsVector(
    ColumnsVector columns_vector);

}  // namespace afc::editor
#endif  // __AFC_EDITOR_COLUMNS_VECTOR_H__
