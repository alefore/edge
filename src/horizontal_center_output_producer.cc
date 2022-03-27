#include "src/horizontal_center_output_producer.h"

#include <glog/logging.h>

#include <algorithm>
#include <cctype>
#include <iostream>

#include "src/vertical_split_output_producer.h"

namespace afc::editor {
using V = VerticalSplitOutputProducer;
namespace {
V::Column GetPadding(ColumnNumberDelta width) {
  return V::Column{.producer = OutputProducer::Empty(), .width = width};
}

class LiteralProducer : public OutputProducer {
 public:
  LiteralProducer(LineWithCursor::Generator::Vector output)
      : output_(std::move(output)) {}
  LineWithCursor::Generator::Vector Produce(LineNumberDelta) { return output_; }

 private:
  const LineWithCursor::Generator::Vector output_;
};
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

  columns[0] = GetPadding((width_ - delegate_output.width) / 2);
  columns[2] = GetPadding(width_ - delegate_output.width - *columns[0].width);

  auto width = delegate_output.width;
  columns[1] = {
      .producer = std::make_unique<LiteralProducer>(std::move(delegate_output)),
      .width = width};
  return V(std::move(columns), 1).Produce(lines);
}

}  // namespace afc::editor
