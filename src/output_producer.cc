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
      : generator_(Generator::New(CaptureAndHash(
            [](LineWithCursor line) { return line; }, std::move(line)))) {}

  Generator Next() override { return generator_; }

 private:
  const Generator generator_;
};
}  // namespace

/* static */ OutputProducer::LineWithCursor
OutputProducer::LineWithCursor::Empty() {
  return OutputProducer::LineWithCursor{std::make_shared<Line>(), std::nullopt};
}

/* static */ std::unique_ptr<OutputProducer> OutputProducer::Empty() {
  return OutputProducer::Constant(LineWithCursor::Empty());
}

/* static */ std::unique_ptr<OutputProducer> OutputProducer::Constant(
    LineWithCursor output) {
  return std::make_unique<ConstantProducer>(output);
}

}  // namespace afc::editor
namespace std {
std::size_t hash<afc::editor::OutputProducer::LineWithCursor>::operator()(
    const afc::editor::OutputProducer::LineWithCursor& line) const {
  return afc::editor::compute_hash(*line.line, line.cursor);
}
}  // namespace std
