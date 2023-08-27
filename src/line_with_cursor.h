#ifndef __AFC_EDITOR_LINE_WITH_CURSOR_H__
#define __AFC_EDITOR_LINE_WITH_CURSOR_H__

#include <functional>
#include <optional>
#include <vector>

#include "src/language/hash.h"
#include "src/language/safe_types.h"
#include "src/language/text/line_column.h"
#include "src/line.h"

namespace afc::editor {
class Line;

struct LineWithCursor {
  // Callback that can generate a single line of output.
  struct Generator {
    struct Vector {
      language::text::LineNumberDelta size() const {
        return language::text::LineNumberDelta(lines.size());
      }
      bool empty() const { return lines.empty(); }
      Vector& resize(language::text::LineNumberDelta size);

      Vector& PrependEmptyLines(language::text::LineNumberDelta size);

      // Complexity is linear to the length of `tail`.
      Vector& Append(LineWithCursor::Generator::Vector tail);

      Vector& RemoveCursor();

      std::vector<Generator> lines;
      language::lazy_string::ColumnNumberDelta width =
          language::lazy_string::ColumnNumberDelta();
    };

    static Generator Empty() {
      return Generator{std::nullopt, []() { return LineWithCursor{}; }};
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

  struct ViewOptions {
    const Line& line;

    language::lazy_string::ColumnNumber initial_column;
    // Total number of screen characters to consume. If the input has wide
    // characters, they have to be taken into account (in other words, the
    // number of characters consumed from the input may be smaller than the
    // width).
    language::lazy_string::ColumnNumberDelta width;
    // Maximum number of characters in the input to consume. Even if more
    // characters would fit in the output (per `width`), can stop outputting
    // when this limit is reached.
    language::lazy_string::ColumnNumberDelta input_width;
    std::optional<language::lazy_string::ColumnNumber> active_cursor_column =
        std::nullopt;
    std::set<language::lazy_string::ColumnNumber> inactive_cursor_columns = {};
    LineModifierSet modifiers_main_cursor = {};
    LineModifierSet modifiers_inactive_cursors = {};
  };
  static LineWithCursor View(const ViewOptions& options);

  language::NonNull<std::shared_ptr<Line>> line;

  // Output parameter. If the active cursor is found in the line, stores here
  // the column in which it was output here. May be nullptr.
  std::optional<language::lazy_string::ColumnNumber> cursor = std::nullopt;
};

LineWithCursor::Generator::Vector RepeatLine(
    LineWithCursor line, language::text::LineNumberDelta times);

}  // namespace afc::editor
namespace std {
template <>
struct hash<afc::editor::LineWithCursor> {
  std::size_t operator()(const afc::editor::LineWithCursor& line) const;
};
}  // namespace std
#endif  // __AFC_EDITOR_LINE_WITH_CURSOR_H__
