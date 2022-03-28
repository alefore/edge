#ifndef __AFC_EDITOR_OUTPUT_PRODUCER_H__
#define __AFC_EDITOR_OUTPUT_PRODUCER_H__

#include <functional>
#include <optional>
#include <vector>

#include "src/hash.h"
#include "src/line_column.h"

namespace afc::editor {

class Line;

struct LineWithCursor {
  // Callback that can generate a single line of output.
  struct Generator {
    struct Vector {
      LineNumberDelta size() const { return LineNumberDelta(lines.size()); }
      bool empty() const { return lines.empty(); }
      std::vector<Generator> lines;
      ColumnNumberDelta width;
    };

    static Generator Empty() {
      return Generator{std::nullopt, []() { return LineWithCursor::Empty(); }};
    }

    template <typename Callable>
    static Generator New(CallableWithCapture<Callable> callable_with_capture) {
      return Generator{.inputs_hash = callable_with_capture.hash,
                       .generate = std::move(callable_with_capture.callable)};
    }

    // If a value is provided, this should be a hash of all the inputs from
    // which the line is generated. This will used to avoid unnecessarily
    // generating memoized lines.
    std::optional<size_t> inputs_hash;

    // Generates the line. Must be called at most once.
    std::function<LineWithCursor()> generate;
  };

  explicit LineWithCursor(Line line);
  LineWithCursor() = default;

  static LineWithCursor Empty();

  std::shared_ptr<Line> line;

  // Output parameter. If the active cursor is found in the line, stores here
  // the column in which it was output here. May be nullptr.
  std::optional<ColumnNumber> cursor = std::nullopt;
};

// Can be used to render a view of something once, line by line.
class OutputProducer {
 public:
  static std::unique_ptr<OutputProducer> Empty();
  static std::unique_ptr<OutputProducer> Constant(LineWithCursor output);

  virtual LineWithCursor::Generator::Vector Produce(LineNumberDelta lines) = 0;

  static std::function<LineWithCursor::Generator::Vector(LineNumberDelta)>
  ToCallback(std::shared_ptr<OutputProducer> producer);
};

LineWithCursor::Generator::Vector RepeatLine(LineWithCursor line,
                                             LineNumberDelta times);

}  // namespace afc::editor
namespace std {
template <>
struct hash<afc::editor::LineWithCursor> {
  std::size_t operator()(const afc::editor::LineWithCursor& line) const;
};
}  // namespace std
#endif  // __AFC_EDITOR_OUTPUT_PRODUCER_H__
