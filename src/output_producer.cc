#include "src/output_producer.h"

#include <glog/logging.h>

#include "src/hash.h"
#include "src/line.h"
#include "src/line_column.h"

namespace afc::editor {
namespace {
class ConstantProducer : public OutputProducer {
 public:
  ConstantProducer(LineWithCursor line)
      : width_(line.line->contents()->size()),
        generator_(LineWithCursor::Generator::New(CaptureAndHash(
            [](LineWithCursor line) { return line; }, std::move(line)))) {}

  LineWithCursor::Generator::Vector Produce(LineNumberDelta lines) override {
    return LineWithCursor::Generator::Vector{
        .lines = std::vector<LineWithCursor::Generator>(lines.line_delta,
                                                        generator_),
        .width = width_};
  }

 private:
  const ColumnNumberDelta width_;
  const LineWithCursor::Generator generator_;
};
}  // namespace

LineWithCursor::LineWithCursor(Line line)
    : line(std::make_shared<Line>(std::move(line))){};

/* static */ LineWithCursor LineWithCursor::Empty() {
  return LineWithCursor(Line());
}

/* static */ std::unique_ptr<OutputProducer> OutputProducer::Empty() {
  return OutputProducer::Constant(LineWithCursor::Empty());
}

/* static */ std::unique_ptr<OutputProducer> OutputProducer::Constant(
    LineWithCursor output) {
  return std::make_unique<ConstantProducer>(output);
}

/* static */ std::function<LineWithCursor::Generator::Vector(LineNumberDelta)>
OutputProducer::ToCallback(std::shared_ptr<OutputProducer> producer) {
  return [producer](LineNumberDelta lines) { return producer->Produce(lines); };
}

LineWithCursor::Generator::Vector RepeatLine(LineWithCursor line,
                                             LineNumberDelta times) {
  return LineWithCursor::Generator::Vector{
      .lines = std::vector(
          times.line_delta,
          LineWithCursor::Generator{.inputs_hash = {},
                                    .generate = [line] { return line; }}),
      .width = line.line->contents()->size()};
}
}  // namespace afc::editor
namespace std {
std::size_t hash<afc::editor::LineWithCursor>::operator()(
    const afc::editor::LineWithCursor& line) const {
  return afc::editor::compute_hash(*line.line, line.cursor);
}
}  // namespace std
