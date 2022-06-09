#ifndef __AFC_EDITOR_TRANSFORMATION_SET_POSITION_H__
#define __AFC_EDITOR_TRANSFORMATION_SET_POSITION_H__

#include <memory>

#include "src/futures/futures.h"
#include "src/line_column.h"
#include "src/transformation/input.h"
#include "src/transformation/result.h"
#include "src/vm/public/environment.h"

namespace afc::editor::transformation {
struct SetPosition {
  explicit SetPosition(LineColumn position)
      : line(position.line), column(position.column) {}
  explicit SetPosition(language::lazy_string::ColumnNumber input_column)
      : column(input_column) {}

  std::optional<LineNumber> line;
  // If column is greater than the length of the line, goes to the end of the
  // line.
  language::lazy_string::ColumnNumber column;
};

void RegisterSetPosition(language::gc::Pool& pool,
                         vm::Environment& environment);

futures::Value<Result> ApplyBase(const SetPosition& parameters, Input input);
std::wstring ToStringBase(const SetPosition& parameters);
SetPosition OptimizeBase(SetPosition transformation);
}  // namespace afc::editor::transformation

#endif  // __AFC_EDITOR_TRANSFORMATION_SET_POSITION_H__
