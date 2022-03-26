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
        generator_(Generator::New(CaptureAndHash(
            [](LineWithCursor line) { return line; }, std::move(line)))) {}

  Output Produce(LineNumberDelta lines) override {
    return Output{.lines = std::vector<Generator>(lines.line_delta, generator_),
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

/* static */ std::function<OutputProducer::Output(LineNumberDelta)>
OutputProducer::ToCallback(std::shared_ptr<OutputProducer> producer) {
  return [producer](LineNumberDelta lines) { return producer->Produce(lines); };
}

OutputProducer::Output RepeatLine(LineWithCursor line, LineNumberDelta times) {
  return OutputProducer::Output{
      .lines = std::vector(
          times.line_delta,
          OutputProducer::Generator{.inputs_hash = {},
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
