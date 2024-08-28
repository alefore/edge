#include "src/columns_vector.h"

#include <glog/logging.h>

#include <algorithm>
#include <cctype>
#include <iostream>

#include "src/language/container.h"
#include "src/language/lazy_string/append.h"
#include "src/language/lazy_string/char_buffer.h"
#include "src/language/lazy_string/lazy_string.h"
#include "src/language/text/line.h"
#include "src/language/text/line_builder.h"
#include "src/language/wstring.h"
#include "src/tests/tests.h"

namespace container = afc::language::container;
using afc::infrastructure::screen::LineModifierSet;
using afc::language::compute_hash;
using afc::language::MakeHashableIteratorRange;
using afc::language::MakeNonNullShared;
using afc::language::NonNull;
using afc::language::lazy_string::ColumnNumber;
using afc::language::lazy_string::ColumnNumberDelta;
using afc::language::lazy_string::LazyString;
using afc::language::text::Line;
using afc::language::text::LineBuilder;
using afc::language::text::LineNumber;
using afc::language::text::LineNumberDelta;

namespace afc::editor {
namespace {

std::optional<size_t> CombineHashes(
    const std::vector<LineWithCursor::Generator>& delegates,
    const ColumnsVector& columns_vector) {
  return std::find_if(delegates.begin(), delegates.end(),
                      [](const LineWithCursor::Generator& g) {
                        return !g.inputs_hash.has_value();
                      }) != delegates.end()
             ? std::optional<size_t>()
             : compute_hash(MakeHashableIteratorRange(
                                delegates.begin(), delegates.end(),
                                [](const LineWithCursor::Generator& g) {
                                  return *g.inputs_hash;
                                }),
                            MakeHashableIteratorRange(
                                columns_vector.columns.begin(),
                                columns_vector.columns.end(),
                                [](const ColumnsVector::Column& column) {
                                  return compute_hash(column.width);
                                }));
}

LineBuilder GeneratePadding(const ColumnsVector::Padding padding,
                            ColumnNumberDelta size) {
  LineBuilder options;
  CHECK(!padding.body.size().IsZero());
  LazyString contents = padding.head;
  while (contents.size() < size)
    contents = std::move(contents).Append(padding.body);
  options.AppendString(std::move(contents).Substring(ColumnNumber(), size),
                       padding.modifiers);
  return options;
}
}  // namespace

LineWithCursor::Generator::Vector OutputFromColumnsVector(
    ColumnsVector columns_vector_raw) {
  for (const auto& column : columns_vector_raw.columns) {
    for (const auto& p : column.padding) {
      if (p.has_value()) {
        CHECK(!p->body.size().IsZero());
      }
    }
  }
  auto columns_vector =
      std::make_shared<ColumnsVector>(std::move(columns_vector_raw));
  std::vector<LineWithCursor::Generator::Vector> inputs_by_column =
      container::MaterializeVector(
          columns_vector->columns |
          std::views::transform(&ColumnsVector::Column::lines));

  LineWithCursor::Generator::Vector output;
  for (size_t i = 0; i < columns_vector->columns.size(); i++) {
    const ColumnsVector::Column& column = columns_vector->columns.at(i);
    if (column.width.has_value()) {
      output.width += *column.width;
    } else {
      output.width += inputs_by_column[i].width;
      break;  // This is the last column.
    }
  }

  LineNumberDelta lines_longest_column;
  for (LineWithCursor::Generator::Vector& input : inputs_by_column)
    lines_longest_column = std::max(lines_longest_column, input.size());

  // Outer index is the line being produced; inner index is the column.
  std::vector<std::vector<LineWithCursor::Generator>> generator_by_line_column(
      lines_longest_column.read(),
      std::vector<LineWithCursor::Generator>(
          inputs_by_column.size(), LineWithCursor::Generator::Empty()));

  for (size_t column_index = 0; column_index < inputs_by_column.size();
       ++column_index) {
    LineWithCursor::Generator::Vector& input = inputs_by_column[column_index];
    for (LineNumberDelta i; i < input.size(); ++i) {
      generator_by_line_column[i.read()][column_index] =
          std::move(input.lines[i.read()]);
    }
  }

  for (LineNumber line;
       line.ToDelta() < LineNumberDelta(generator_by_line_column.size());
       ++line) {
    auto& line_input = generator_by_line_column[line.read()];
    output.lines.push_back(LineWithCursor::Generator{
        .inputs_hash = CombineHashes(line_input, *columns_vector),
        .generate = [line, line_input = std::move(line_input),
                     columns_vector]() {
          std::optional<ColumnNumber> cursor;
          LineBuilder options;
          ColumnNumber initial_column;
          LineModifierSet current_modifiers;
          // This takes wide characters into account (i.e., it may differ from
          // options.EndColumn() when there are wide characters).
          ColumnNumber columns_shown;
          for (size_t i = 0; i < line_input.size(); i++) {
            ColumnNumberDelta padding_needed = initial_column - columns_shown;
            if (columns_vector->columns[i].padding.size() > line.read() &&
                columns_vector->columns[i].padding[line.read()].has_value()) {
              options.Append(GeneratePadding(
                  *columns_vector->columns[i].padding[line.read()],
                  padding_needed));
            } else if (padding_needed > ColumnNumberDelta(0)) {
              options.AppendString(LazyString{padding_needed, L' '},
                                   current_modifiers);
            }
            columns_shown = initial_column;

            LineWithCursor column_data = line_input[i].generate();
            if (column_data.cursor.has_value() &&
                i == columns_vector->index_active) {
              cursor = initial_column + column_data.cursor.value().ToDelta();
            }

            current_modifiers = column_data.line.end_of_line_modifiers();

            if (columns_vector->columns.at(i).width.has_value()) {
              // TODO: respect columns_[i].width.
              initial_column += columns_vector->columns.at(i).width.value();
            } else {
              i = line_input.size();  // Stop the iteration.
            }
            auto str = column_data.line.ToString();
            columns_shown += ColumnNumberDelta(
                std::max(0, wcswidth(str.c_str(), str.size())));
            options.Append(LineBuilder(std::move(column_data.line)));
          }
          return LineWithCursor{.line = std::move(options).Build(),
                                .cursor = cursor};
        }});
  }
  return output;
}

namespace {
const bool buffer_tests_registration = tests::Register(
    L"OutputFromColumnsVector",
    {{.name = L"UseAfterDelete",
      .callback =
          [] {
            ColumnsVector columns_vector;
            for (int i = 0; i < 5; i++)
              columns_vector.push_back(
                  {.lines = RepeatLine({.line = Line(L"foo bar")},
                                       LineNumberDelta(5)),
                   .width = ColumnNumberDelta(10)});
            LineWithCursor::Generator::Vector produce =
                OutputFromColumnsVector(std::move(columns_vector));
            columns_vector.columns = {};
            CHECK_EQ(produce.size(), LineNumberDelta(5));
            CHECK(produce.lines[0].generate().line.contents() ==
                  LazyString{L"foo bar   "
                             L"foo bar   "
                             L"foo bar   "
                             L"foo bar   "
                             L"foo bar"});
          }},
     {.name = L"ShortColumns",
      .callback =
          [] {
            ColumnsVector columns_vector;
            columns_vector.push_back(
                {.lines =
                     RepeatLine({.line = Line(L"foo")}, LineNumberDelta(1)),
                 .width = ColumnNumberDelta(3)});
            columns_vector.push_back(
                {.lines =
                     RepeatLine({.line = Line(L"bar")}, LineNumberDelta(10)),
                 .width = ColumnNumberDelta(10)});
            LineWithCursor::Generator::Vector output =
                OutputFromColumnsVector(std::move(columns_vector));
            CHECK_EQ(output.size(), LineNumberDelta(10));
            CHECK_EQ(output.lines[0].generate().line.contents(),
                     LazyString{L"foobar"});
            CHECK_EQ(output.lines[1].generate().line.contents(),
                     LazyString{L"   bar"});
            CHECK_EQ(output.lines[9].generate().line.contents(),
                     LazyString{L"   bar"});
          }},
     {.name = L"ShortPadding", .callback = [] {
        ColumnsVector columns_vector;
        columns_vector.push_back(
            ColumnsVector::Column{.lines = {}, .width = ColumnNumberDelta(5)});
        columns_vector.push_back(ColumnsVector::Column{
            .lines = RepeatLine({.line = Line(L"bar")}, LineNumberDelta(10)),
            .padding = {std::vector<std::optional<ColumnsVector::Padding>>(
                5, ColumnsVector::Padding{.modifiers = {},
                                          .head = LazyString(),
                                          .body = LazyString{L"Foo"}})}});
        LineWithCursor::Generator::Vector output =
            OutputFromColumnsVector(std::move(columns_vector));
        for (auto& entry : output.lines) entry.generate();
      }}});

}  // namespace
}  // namespace afc::editor
