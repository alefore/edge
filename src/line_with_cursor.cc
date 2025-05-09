#include "src/line_with_cursor.h"

#include <glog/logging.h>

#include "src/infrastructure/tracker.h"
#include "src/language/container.h"
#include "src/language/hash.h"
#include "src/language/lazy_string/append.h"
#include "src/language/lazy_string/char_buffer.h"
#include "src/language/safe_types.h"
#include "src/language/text/line.h"
#include "src/language/text/line_builder.h"
#include "src/language/text/line_column.h"
#include "src/tests/tests.h"

namespace container = afc::language::container;
using afc::infrastructure::screen::LineModifierSet;
using afc::language::MakeNonNullShared;
using afc::language::lazy_string::ColumnNumber;
using afc::language::lazy_string::ColumnNumberDelta;
using afc::language::lazy_string::LazyString;
using afc::language::lazy_string::SingleLine;
using afc::language::text::Line;
using afc::language::text::LineBuilder;
using afc::language::text::LineNumberDelta;

namespace afc::editor {
using ::operator<<;

LineWithCursor::Generator::Vector& LineWithCursor::Generator::Vector::resize(
    LineNumberDelta size) {
  lines.resize(size.read(), Generator::Empty());
  return *this;
}

LineWithCursor::Generator::Vector&
LineWithCursor::Generator::Vector::PrependEmptyLines(LineNumberDelta size) {
  std::vector<LineWithCursor::Generator> prefix(
      size.read(), LineWithCursor::Generator::Empty());
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
const bool tests_registration = tests::Register(
    L"LineWithCursor::Generator::Vector::Append", std::invoke([] {
      auto Build = [](LineWithCursor::Generator::Vector rows) {
        return container::MaterializeVector(
            rows.lines | std::views::transform([](LineWithCursor::Generator g) {
              return g.generate().line.contents();
            }));
      };
      return std::vector<tests::Test>{
          {.name = L"TwoRowsShort", .callback = [&] {
             auto output = Build(
                 RepeatLine(LineWithCursor{.line = Line{SingleLine{
                                               LazyString{L"top"}}}},
                            LineNumberDelta(2))
                     .Append(RepeatLine(
                         LineWithCursor{
                             .line = Line{SingleLine{LazyString{L"bottom"}}}},
                         LineNumberDelta(2))));
             CHECK_EQ(output.size(), 4ul);
             CHECK_EQ(output[0], SingleLine{LazyString{L"top"}});
             CHECK_EQ(output[1], SingleLine{LazyString{L"top"}});
             CHECK_EQ(output[2], SingleLine{LazyString{L"bottom"}});
             CHECK_EQ(output[3], SingleLine{LazyString{L"bottom"}});
           }}};
    }));
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

LineWithCursor::Generator::Vector RepeatLine(LineWithCursor line,
                                             LineNumberDelta times) {
  return LineWithCursor::Generator::Vector{
      .lines = std::vector(
          times.read(),
          LineWithCursor::Generator{.inputs_hash = {},
                                    .generate = [line] { return line; }}),
      .width = line.line.contents().size()};
}

/* static */
LineWithCursor LineWithCursor::View(
    const LineWithCursor::ViewOptions& options) {
  TRACK_OPERATION(LineWithCursor_View);

  VLOG(5) << "Producing output of line: " << options.line.ToString();

  LineBuilder line_output;
  ColumnNumber input_column = options.initial_column;
  LineWithCursor line_with_cursor;
  auto modifiers_it = options.line.modifiers().lower_bound(input_column);
  if (!options.line.modifiers().empty() &&
      modifiers_it != options.line.modifiers().begin()) {
    line_output.set_modifiers(ColumnNumber(), std::prev(modifiers_it)->second);
  }

  const ColumnNumber input_end =
      options.input_width != std::numeric_limits<ColumnNumberDelta>::max()
          ? std::min(options.line.EndColumn(),
                     input_column + options.input_width)
          : options.line.EndColumn();
  // output_column contains the column in the screen. May not match
  // options.contents().size() if there are wide characters.
  for (ColumnNumber output_column;
       input_column <= input_end && output_column.ToDelta() < options.width;
       ++input_column) {
    wchar_t c =
        input_column < input_end ? options.line.get(input_column) : L' ';
    CHECK(c != '\n');

    ColumnNumber current_position =
        ColumnNumber() + line_output.contents().size();
    if (modifiers_it != options.line.modifiers().end()) {
      CHECK_GE(modifiers_it->first, input_column);
      if (modifiers_it->first == input_column) {
        line_output.set_modifiers(current_position, modifiers_it->second);
        ++modifiers_it;
      }
    }

    if (options.active_cursor_column.has_value() &&
        (options.active_cursor_column.value() == input_column ||
         (input_column == input_end &&
          options.active_cursor_column.value() >= input_column))) {
      // We use current_position rather than output_column because terminals
      // compensate for wide characters (so we don't need to).
      line_with_cursor.cursor = current_position;
      if (!options.modifiers_main_cursor.empty()) {
        line_output.set_modifiers(current_position + ColumnNumberDelta(1),
                                  line_output.modifiers_empty()
                                      ? LineModifierSet()
                                      : line_output.modifiers_last().second);
        line_output.InsertModifiers(current_position,
                                    options.modifiers_main_cursor);
      }
    } else if (options.inactive_cursor_columns.find(input_column) !=
                   options.inactive_cursor_columns.end() ||
               (input_column == input_end &&
                !options.inactive_cursor_columns.empty() &&
                *options.inactive_cursor_columns.rbegin() >= input_column)) {
      line_output.set_modifiers(current_position + ColumnNumberDelta(1),
                                line_output.modifiers_empty()
                                    ? LineModifierSet()
                                    : line_output.modifiers_last().second);
      line_output.InsertModifiers(current_position,
                                  options.modifiers_inactive_cursors);
    }

    switch (c) {
      case L'\r':
        break;

      case L'\t': {
        ColumnNumber target =
            ColumnNumber(0) +
            ((output_column.ToDelta() / 8) + ColumnNumberDelta(1)) * 8;
        VLOG(8) << "Handling TAB character at position: " << output_column
                << ", target: " << target;
        line_output.AppendString(SingleLine::Padding(target - output_column),
                                 std::nullopt);
        output_column = target;
        break;
      }

      default:
        if (const ColumnNumberDelta c_width{wcwidth(c)};
            c_width != ColumnNumberDelta{-1}) {
          // Sanity check.
          CHECK_GE(c_width, 0);
          CHECK_LT(c_width, 10);
          output_column += c_width;
          if (output_column.ToDelta() <= options.width)
            line_output.set_contents(
                line_output.contents() +
                SingleLine{LazyString{ColumnNumberDelta{1}, c}});
        } else {
          VLOG(5) << "wcwidth returned -1: " << static_cast<int>(c);
        }
    }
  }

  line_output.set_end_of_line_modifiers(
      input_column == options.line.EndColumn()
          ? options.line.end_of_line_modifiers()
          : (line_output.modifiers_empty()
                 ? LineModifierSet()
                 : line_output.modifiers_last().second));
  if (!line_with_cursor.cursor.has_value() &&
      options.active_cursor_column.has_value()) {
    // Same as above: we use the current position (rather than output_column)
    // since terminals compensate for wide characters.
    line_with_cursor.cursor = ColumnNumber() + line_output.contents().size();
  }

  line_with_cursor.line = std::move(line_output).Build();
  return line_with_cursor;
}
}  // namespace afc::editor
namespace std {
std::size_t hash<afc::editor::LineWithCursor>::operator()(
    const afc::editor::LineWithCursor& line) const {
  return afc::language::compute_hash(line.line, line.cursor);
}
}  // namespace std
