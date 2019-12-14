#include "src/output_producer.h"

#include <glog/logging.h>

#include "src/line.h"

namespace afc::editor {
namespace {
class EmptyProducer : public OutputProducer {
  Generator Next() override {
    return Generator{0ul, []() { return LineWithCursor::Empty(); }};
  }
};
}  // namespace

/* static */ OutputProducer::LineWithCursor
OutputProducer::LineWithCursor::Empty() {
  return OutputProducer::LineWithCursor{std::make_shared<Line>(), std::nullopt};
}

/* static */ std::unique_ptr<OutputProducer> OutputProducer::Empty() {
  return std::make_unique<EmptyProducer>();
}

}  // namespace afc::editor
