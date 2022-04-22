#ifndef __AFC_EDITOR_COLUMNS_VECTOR_H__
#define __AFC_EDITOR_COLUMNS_VECTOR_H__

#include <memory>
#include <vector>

#include "src/buffer.h"
#include "src/line_with_cursor.h"

namespace afc::editor {

struct ColumnsVector {
  // If present, a column may stretch leftwards as long as the previous column
  // is shorter than its width. In this case, the padding will be a subset of
  // `head` followed by repetitions of `body`.
  struct Padding {
    LineModifierSet modifiers = {};
    language::NonNull<std::shared_ptr<LazyString>> head;
    language::NonNull<std::shared_ptr<LazyString>> body;
  };

  struct Column {
    LineWithCursor::Generator::Vector lines;

    // Optional. Can be empty or shorter than `lines` (or longer, in which case
    // additional elements will be ignored).
    std::vector<std::optional<Padding>> padding = {};

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
};

LineWithCursor::Generator::Vector OutputFromColumnsVector(
    ColumnsVector columns_vector);

}  // namespace afc::editor
#endif  // __AFC_EDITOR_COLUMNS_VECTOR_H__
