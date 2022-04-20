#include "src/columns_vector.h"

#include <glog/logging.h>

#include <algorithm>
#include <cctype>
#include <iostream>

#include "src/language/wstring.h"
#include "src/lazy_string.h"
#include "src/lazy_string_append.h"
#include "src/substring.h"
#include "src/tests/tests.h"

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

Line GeneratePadding(const ColumnsVector::Padding padding,
                     ColumnNumberDelta size) {
  Line::Options options;
  CHECK(!padding.body->size().IsZero());
  std::shared_ptr<LazyString> contents = padding.head;
  while (contents->size() < size) {
    contents = StringAppend(std::move(contents), padding.body);
  }
  options.AppendString(Substring(std::move(contents), ColumnNumber(), size),
                       padding.modifiers);
  return Line(std::move(options));
}
}  // namespace

LineWithCursor::Generator::Vector OutputFromColumnsVector(
    ColumnsVector columns_vector_raw) {
  for (const auto& column : columns_vector_raw.columns) {
    for (const auto& p : column.padding) {
      if (p.has_value()) {
        CHECK(p->head != nullptr);
        CHECK(p->body != nullptr);
        CHECK(!p->body->size().IsZero());
      }
    }
  }
  auto columns_vector =
      std::make_shared<ColumnsVector>(std::move(columns_vector_raw));
  std::vector<LineWithCursor::Generator::Vector> inputs_by_column;
  for (auto& c : columns_vector->columns) {
    inputs_by_column.push_back(std::move(c.lines));
  }

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
    lines_longest_column = max(lines_longest_column, input.size());

  // Outer index is the line being produced; inner index is the column.
  std::vector<std::vector<LineWithCursor::Generator>> generator_by_line_column(
      lines_longest_column.line_delta,
      std::vector<LineWithCursor::Generator>(
          inputs_by_column.size(), LineWithCursor::Generator::Empty()));

  for (size_t column_index = 0; column_index < inputs_by_column.size();
       ++column_index) {
    LineWithCursor::Generator::Vector& input = inputs_by_column[column_index];
    for (LineNumberDelta i; i < input.size(); ++i) {
      generator_by_line_column[i.line_delta][column_index] =
          std::move(input.lines[i.line_delta]);
    }
  }

  for (LineNumber line;
       line.ToDelta() < LineNumberDelta(generator_by_line_column.size());
       ++line) {
    auto& line_input = generator_by_line_column[line.line];
    output.lines.push_back(LineWithCursor::Generator{
        .inputs_hash = CombineHashes(line_input, *columns_vector),
        .generate = [line, line_input = std::move(line_input),
                     columns_vector]() {
          LineWithCursor output;
          Line::Options options;
          ColumnNumber initial_column;
          LineModifierSet current_modifiers;
          // This takes wide characters into account (i.e., it may differ from
          // options.EndColumn() when there are wide characters).
          ColumnNumber columns_shown;
          for (size_t i = 0; i < line_input.size(); i++) {
            ColumnNumberDelta padding_needed = initial_column - columns_shown;
            if (columns_vector->columns[i].padding.size() > i &&
                columns_vector->columns[i].padding[line.line].has_value()) {
              options.Append(GeneratePadding(
                  *columns_vector->columns[i].padding[line.line],
                  padding_needed));
            } else {
              options.AppendString(
                  ColumnNumberDelta::PaddingString(padding_needed, L' '),
                  current_modifiers);
            }
            columns_shown = initial_column;

            LineWithCursor column_data = line_input[i].generate();
            if (column_data.cursor.has_value() &&
                i == columns_vector->index_active) {
              output.cursor =
                  initial_column + column_data.cursor.value().ToDelta();
            }

            current_modifiers = column_data.line->end_of_line_modifiers();

            CHECK(column_data.line != nullptr);
            if (columns_vector->columns.at(i).width.has_value()) {
              // TODO: respect columns_[i].width.
              initial_column += columns_vector->columns.at(i).width.value();
            } else {
              i = line_input.size();  // Stop the iteration.
            }
            auto str = column_data.line->ToString();
            columns_shown += ColumnNumberDelta(
                std::max(0, wcswidth(str.c_str(), str.size())));
            options.Append(std::move(*column_data.line));
          }
          output.line = std::make_shared<Line>(std::move(options));
          return output;
        }});
  }
  return output;
}

namespace {
const bool buffer_tests_registration = tests::Register(
    L"OutputFromColumnsVector",
    {
        {.name = L"UseAfterDelete",
         .callback =
             [] {
               ColumnsVector columns_vector;
               for (int i = 0; i < 5; i++)
                 columns_vector.push_back(
                     {.lines = RepeatLine(LineWithCursor(Line(L"foo bar")),
                                          LineNumberDelta(5)),
                      .width = ColumnNumberDelta(10)});
               LineWithCursor::Generator::Vector produce =
                   OutputFromColumnsVector(std::move(columns_vector));
               columns_vector.columns = {};
               CHECK_EQ(produce.size(), LineNumberDelta(5));
               CHECK(produce.lines[0].generate().line->ToString() ==
                     L"foo bar   "
                     L"foo bar   "
                     L"foo bar   "
                     L"foo bar   "
                     L"foo bar");
             }},
        {.name = L"ShortColumns",
         .callback =
             [] {
               ColumnsVector columns_vector;
               columns_vector.push_back(
                   {.lines = RepeatLine(LineWithCursor(Line(L"foo")),
                                        LineNumberDelta(1)),
                    .width = ColumnNumberDelta(3)});
               columns_vector.push_back(
                   {.lines = RepeatLine(LineWithCursor(Line(L"bar")),
                                        LineNumberDelta(10)),
                    .width = ColumnNumberDelta(10)});
               LineWithCursor::Generator::Vector output =
                   OutputFromColumnsVector(std::move(columns_vector));
               CHECK_EQ(output.size(), LineNumberDelta(10));
               CHECK(output.lines[0].generate().line->ToString() == L"foobar");
               CHECK(output.lines[1].generate().line->ToString() == L"   bar");
               CHECK(output.lines[9].generate().line->ToString() == L"   bar");
             }},
    });

}
}  // namespace afc::editor
