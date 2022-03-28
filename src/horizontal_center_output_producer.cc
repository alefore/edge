#include "src/horizontal_center_output_producer.h"

#include <glog/logging.h>

#include <algorithm>
#include <cctype>
#include <iostream>

#include "src/columns_vector.h"

namespace afc::editor {
using V = ColumnsVector;
namespace {
V::Column GetPadding(LineNumberDelta lines, ColumnNumberDelta width) {
  return V::Column{.lines = RepeatLine(LineWithCursor(Line()), lines),
                   .width = width};
}
}  // namespace

LineWithCursor::Generator::Vector CenterOutput(
    LineWithCursor::Generator::Vector lines, ColumnNumberDelta width) {
  if (lines.width >= width) return lines;

  ColumnsVector columns_vector{.index_active = 1};

  columns_vector.push_back(GetPadding(lines.size(), (width - lines.width) / 2));
  columns_vector.push_back({.lines = lines, .width = lines.width});
  columns_vector.push_back(GetPadding(
      lines.size(), width - lines.width - *columns_vector.columns[0].width));

  return OutputFromColumnsVector(std::move(columns_vector));
}

}  // namespace afc::editor
