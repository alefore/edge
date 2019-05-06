#include "src/output_producer.h"

#include <glog/logging.h>

#include "src/line.h"

namespace afc {
namespace editor {

/* static */ OutputProducer::LineWithCursor
OutputProducer::LineWithCursor::Empty() {
  return OutputProducer::LineWithCursor{std::make_shared<Line>(), std::nullopt};
}

}  // namespace editor
}  // namespace afc
