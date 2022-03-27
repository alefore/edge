#include "src/horizontal_center_output_producer.h"

#include <glog/logging.h>

#include <algorithm>
#include <cctype>
#include <iostream>

#include "src/vertical_split_output_producer.h"

namespace afc::editor {
using V = VerticalSplitOutputProducer;
namespace {
V::Column GetPadding(LineNumberDelta lines, ColumnNumberDelta width) {
  return V::Column{.lines = RepeatLine(LineWithCursor(Line()), lines),
                   .width = width};
}
}  // namespace

LineWithCursor::Generator::Vector CenterOutput(
    LineWithCursor::Generator::Vector lines, ColumnNumberDelta width) {
  if (lines.width >= width) return lines;

  std::vector<V::Column> columns(3);

  columns[0] = GetPadding(lines.size(), (width - lines.width) / 2);
  columns[2] =
      GetPadding(lines.size(), width - lines.width - *columns[0].width);

  columns[1] = {.lines = lines, .width = lines.width};
  return V(std::move(columns), 1).Produce(lines.size());
}

}  // namespace afc::editor
