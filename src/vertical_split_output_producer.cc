#include "src/vertical_split_output_producer.h"

#include <glog/logging.h>

#include <algorithm>
#include <cctype>
#include <iostream>

#include "src/tests/tests.h"
#include "src/wstring.h"

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
}  // namespace

LineWithCursor::Generator::Vector OutputFromColumnsVector(
    ColumnsVector columns_vector_raw) {
  auto columns_vector =
      std::make_shared<ColumnsVector>(std::move(columns_vector_raw));
  std::vector<LineWithCursor::Generator::Vector> inputs_by_column;
  for (auto& c : columns_vector->columns) {
    LineWithCursor::Generator::Vector input = c.lines;
    input.lines.resize(columns_vector->lines.line_delta,
                       LineWithCursor::Generator::Empty());
    inputs_by_column.push_back(std::move(input));
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

  // Outer index is the line being produced; inner index is the column.
  std::vector<std::vector<LineWithCursor::Generator>> generator_by_line_column(
      columns_vector->lines.line_delta);
  for (LineWithCursor::Generator::Vector& input : inputs_by_column) {
    for (LineNumberDelta i; i < input.size(); ++i) {
      generator_by_line_column[i.line_delta].push_back(
          std::move(input.lines[i.line_delta]));
    }
  }

  for (auto& line_input : generator_by_line_column) {
    output.lines.push_back(LineWithCursor::Generator{
        .inputs_hash = CombineHashes(line_input, *columns_vector),
        .generate = [line_input = std::move(line_input), columns_vector]() {
          LineWithCursor output;
          Line::Options options;
          ColumnNumber initial_column;
          LineModifierSet current_modifiers;
          // This takes wide characters into account (i.e., it may differ from
          // options.EndColumn() when there are wide characters).
          ColumnNumber columns_shown;
          for (size_t i = 0; i < line_input.size(); i++) {
            options.AppendString(ColumnNumberDelta::PaddingString(
                                     initial_column - columns_shown, L' '),
                                 current_modifiers);
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
               ColumnsVector columns_vector{.lines = LineNumberDelta(15)};
               for (int i = 0; i < 5; i++)
                 columns_vector.push_back(
                     {.lines = RepeatLine(LineWithCursor(Line(L"foo bar")),
                                          LineNumberDelta(15)),
                      .width = ColumnNumberDelta(10)});
               LineWithCursor::Generator::Vector produce =
                   OutputFromColumnsVector(std::move(columns_vector));
               columns_vector.columns = {};
               CHECK_EQ(produce.size(), LineNumberDelta(15));
               CHECK(produce.lines[0].generate().line->ToString() ==
                     L"foo bar   "
                     L"foo bar   "
                     L"foo bar   "
                     L"foo bar   "
                     L"foo bar");
             }},
    });

}
}  // namespace afc::editor
