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

HorizontalCenterOutputProducer::HorizontalCenterOutputProducer(
    LineWithCursor::Generator::Vector lines, ColumnNumberDelta width)
    : lines_(std::move(lines)), width_(width) {}

LineWithCursor::Generator::Vector HorizontalCenterOutputProducer::Produce(
    LineNumberDelta lines) {
  if (lines_.width >= width_) return lines_;

  std::vector<V::Column> columns(3);

  columns[0] = GetPadding(lines, (width_ - lines_.width) / 2);
  columns[2] = GetPadding(lines, width_ - lines_.width - *columns[0].width);

  auto width = lines_.width;
  columns[1] = {.lines = lines_, .width = width};
  return V(std::move(columns), 1).Produce(lines);
}

}  // namespace afc::editor
