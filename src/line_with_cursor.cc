#include "src/line_with_cursor.h"

#include <glog/logging.h>

#include "src/infrastructure/tracker.h"
#include "src/language/hash.h"
#include "src/language/lazy_string/append.h"
#include "src/language/lazy_string/char_buffer.h"
#include "src/language/lazy_string/padding.h"
#include "src/language/safe_types.h"
#include "src/language/text/line_column.h"
#include "src/line.h"
#include "src/tests/tests.h"
namespace afc::editor {
using ::operator<<;

using infrastructure::Tracker;
using language::MakeNonNullShared;
using language::lazy_string::ColumnNumber;
using language::lazy_string::ColumnNumberDelta;
using language::lazy_string::NewLazyString;
using language::text::Line;
using language::text::LineBuilder;
using language::text::LineNumberDelta;

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
                 RepeatLine(
                     LineWithCursor{.line = MakeNonNullShared<Line>(L"top")},
                     LineNumberDelta(2))
                     .Append(RepeatLine(
                         LineWithCursor{.line =
                                            MakeNonNullShared<Line>(L"bottom")},
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

LineWithCursor::Generator::Vector RepeatLine(LineWithCursor line,
                                             LineNumberDelta times) {
  return LineWithCursor::Generator::Vector{
      .lines = std::vector(
          times.read(),
          LineWithCursor::Generator{.inputs_hash = {},
                                    .generate = [line] { return line; }}),
      .width = line.line->contents()->size()};
}

/* static */
LineWithCursor LineWithCursor::View(
    const LineWithCursor::ViewOptions& options) {
  static Tracker tracker(L"LineWithCursor::View");
  auto tracker_call = tracker.Call();

  VLOG(5) << "Producing output of line: " << options.line.ToString();

  LineBuilder line_output;
  ColumnNumber input_column = options.initial_column;
  LineWithCursor line_with_cursor;
  auto modifiers_it = options.line.data_.modifiers.lower_bound(input_column);
  if (!options.line.data_.modifiers.empty() &&
      modifiers_it != options.line.data_.modifiers.begin()) {
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
    wint_t c = input_column < input_end ? options.line.get(input_column) : L' ';
    CHECK(c != '\n');

    ColumnNumber current_position =
        ColumnNumber() + line_output.contents()->size();
    if (modifiers_it != options.line.data_.modifiers.end()) {
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
        line_output.set_modifiers(
            current_position + ColumnNumberDelta(1),
            line_output.data_.modifiers.empty()
                ? LineModifierSet()
                : line_output.data_.modifiers.rbegin()->second);
        line_output.data_.modifiers[current_position].insert(
            options.modifiers_main_cursor.begin(),
            options.modifiers_main_cursor.end());
      }
    } else if (options.inactive_cursor_columns.find(input_column) !=
                   options.inactive_cursor_columns.end() ||
               (input_column == input_end &&
                !options.inactive_cursor_columns.empty() &&
                *options.inactive_cursor_columns.rbegin() >= input_column)) {
      line_output.data_.modifiers[current_position + ColumnNumberDelta(1)] =
          line_output.data_.modifiers.empty()
              ? LineModifierSet()
              : line_output.data_.modifiers.rbegin()->second;
      line_output.data_.modifiers[current_position].insert(
          options.modifiers_inactive_cursors.begin(),
          options.modifiers_inactive_cursors.end());
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
        line_output.AppendString(Padding(target - output_column, L' '),
                                 std::nullopt);
        output_column = target;
        break;
      }

      default:
        VLOG(8) << "Print character: " << c;
        output_column += ColumnNumberDelta(wcwidth(c));
        if (output_column.ToDelta() <= options.width)
          line_output.set_contents(Append(std::move(line_output.contents()),
                                          NewLazyString(std::wstring(1, c))));
    }
  }

  line_output.data_.end_of_line_modifiers =
      input_column == options.line.EndColumn()
          ? options.line.data_.end_of_line_modifiers
          : (line_output.data_.modifiers.empty()
                 ? LineModifierSet()
                 : line_output.data_.modifiers.rbegin()->second);
  if (!line_with_cursor.cursor.has_value() &&
      options.active_cursor_column.has_value()) {
    // Same as above: we use the current position (rather than output_column)
    // since terminals compensate for wide characters.
    line_with_cursor.cursor = ColumnNumber() + line_output.contents()->size();
  }

  line_with_cursor.line =
      MakeNonNullShared<Line>(std::move(line_output).Build());
  return line_with_cursor;
}
}  // namespace afc::editor
namespace std {
std::size_t hash<afc::editor::LineWithCursor>::operator()(
    const afc::editor::LineWithCursor& line) const {
  return afc::language::compute_hash(line.line.value(), line.cursor);
}
}  // namespace std
