#include "src/output_producer.h"

#include <glog/logging.h>

#include "src/hash.h"
#include "src/line.h"
#include "src/line_column.h"

namespace afc::editor {
LineWithCursor::LineWithCursor(Line line)
    : line(std::make_shared<Line>(std::move(line))){};

/* static */ LineWithCursor LineWithCursor::Empty() {
  return LineWithCursor(Line());
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
