#include "src/line_with_cursor.h"

#include <glog/logging.h>

#include "src/language/hash.h"
#include "src/line.h"
#include "src/line_column.h"
#include "src/tests/tests.h"

namespace afc::editor {
LineWithCursor::Generator::Vector& LineWithCursor::Generator::Vector::resize(
    LineNumberDelta size) {
  lines.resize(size.line_delta, Generator::Empty());
  return *this;
}

LineWithCursor::Generator::Vector&
LineWithCursor::Generator::Vector::PrependEmptyLines(LineNumberDelta size) {
  std::vector<LineWithCursor::Generator> prefix(
      size.line_delta, LineWithCursor::Generator::Empty());
  lines.insert(lines.begin(), prefix.begin(), prefix.end());
  return *this;
}

// Complexity is linear to the length of `tail`.
LineWithCursor::Generator::Vector& LineWithCursor::Generator::Vector::Append(
    LineWithCursor::Generator::Vector tail) {
  width = std::max(width, tail.width);
  lines.insert(lines.end(), std::make_move_iterator(tail.lines.begin()),
               std::make_move_iterator(tail.lines.end()));
  return *this;
}

namespace {
const bool tests_registration =
    tests::Register(L"LineWithCursor::Generator::Vector::Append", [] {
      auto Build = [](LineWithCursor::Generator::Vector rows) {
        std::vector<std::wstring> output;
        for (auto& g : rows.lines)
          output.push_back(g.generate().line->ToString());
        return output;
      };
      return std::vector<tests::Test>{
          {.name = L"TwoRowsShort", .callback = [&] {
             auto output = Build(
                 RepeatLine(LineWithCursor(Line(L"top")), LineNumberDelta(2))
                     .Append(RepeatLine(LineWithCursor(Line(L"bottom")),
                                        LineNumberDelta(2))));
             CHECK_EQ(output.size(), 4ul);
             CHECK(output[0] == L"top");
             CHECK(output[1] == L"top");
             CHECK(output[2] == L"bottom");
             CHECK(output[3] == L"bottom");
           }}};
    }());
}  // namespace

LineWithCursor::Generator::Vector&
LineWithCursor::Generator::Vector::RemoveCursor() {
  for (auto& generator : lines) {
    if (generator.inputs_hash.has_value()) {
      generator.inputs_hash =
          std::hash<size_t>{}(generator.inputs_hash.value()) +
          std::hash<size_t>{}(329ul);
    }
    generator.generate = [generate = std::move(generator.generate)] {
      auto output = generate();
      output.cursor = std::nullopt;
      return output;
    };
  }
  return *this;
}

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
