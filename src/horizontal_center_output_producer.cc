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
    std::unique_ptr<OutputProducer> delegate, ColumnNumberDelta width)
    : delegate_(std::move(delegate)), width_(width) {}

LineWithCursor::Generator::Vector HorizontalCenterOutputProducer::Produce(
    LineNumberDelta lines) {
  CHECK(delegate_ != nullptr);
  auto delegate_output = delegate_->Produce(lines);
  if (delegate_output.width >= width_) return delegate_output;

  std::vector<V::Column> columns(3);

  columns[0] = GetPadding(lines, (width_ - delegate_output.width) / 2);
  columns[2] =
      GetPadding(lines, width_ - delegate_output.width - *columns[0].width);

  auto width = delegate_output.width;
  columns[1] = {.lines = delegate_output, .width = width};
  return V(std::move(columns), 1).Produce(lines);
}

}  // namespace afc::editor
