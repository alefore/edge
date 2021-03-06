#ifndef __AFC_EDITOR_TRANSFORMATION_SET_POSITION_H__
#define __AFC_EDITOR_TRANSFORMATION_SET_POSITION_H__

#include <memory>

#include "src/futures/futures.h"
#include "src/line_column.h"
#include "src/transformation/input.h"
#include "src/transformation/result.h"

namespace afc::editor::transformation {
struct SetPosition {
  explicit SetPosition(LineColumn position)
      : line(position.line), column(position.column) {}
  explicit SetPosition(ColumnNumber column) : column(column) {}

  std::optional<LineNumber> line;
  // If column is greater than the length of the line, goes to the end of the
  // line.
  ColumnNumber column;
};

void RegisterSetPosition(vm::Environment* environment);

futures::Value<Result> ApplyBase(const SetPosition& parameters, Input input);
std::wstring ToStringBase(const SetPosition& parameters);

}  // namespace afc::editor::transformation

#endif  // __AFC_EDITOR_TRANSFORMATION_SET_POSITION_H__
