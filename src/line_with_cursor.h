#ifndef __AFC_EDITOR_LINE_WITH_CURSOR_H__
#define __AFC_EDITOR_LINE_WITH_CURSOR_H__

#include <functional>
#include <optional>
#include <vector>

#include "src/language/hash.h"
#include "src/language/safe_types.h"
#include "src/line_column.h"

namespace afc::editor {

class Line;

struct LineWithCursor {
  // Callback that can generate a single line of output.
  struct Generator {
    struct Vector {
      LineNumberDelta size() const { return LineNumberDelta(lines.size()); }
      bool empty() const { return lines.empty(); }
      Vector& resize(LineNumberDelta size);

      Vector& PrependEmptyLines(LineNumberDelta size);

      // Complexity is linear to the length of `tail`.
      Vector& Append(LineWithCursor::Generator::Vector tail);

      Vector& RemoveCursor();

      std::vector<Generator> lines;
      ColumnNumberDelta width;
    };

    static Generator Empty() {
      return Generator{std::nullopt, []() { return LineWithCursor::Empty(); }};
    }

    template <typename Callable>
    static Generator New(
        language::CallableWithCapture<Callable> callable_with_capture) {
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

  static LineWithCursor Empty();

  language::NonNull<std::shared_ptr<Line>> line =
      language::MakeNonNullShared<Line>();

  // Output parameter. If the active cursor is found in the line, stores here
  // the column in which it was output here. May be nullptr.
  std::optional<ColumnNumber> cursor = std::nullopt;
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
#endif  // __AFC_EDITOR_LINE_WITH_CURSOR_H__
