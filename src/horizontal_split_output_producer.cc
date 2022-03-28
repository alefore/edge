#include "src/horizontal_split_output_producer.h"

#include <glog/logging.h>

#include <algorithm>
#include <cctype>
#include <iostream>

#include "src/tests/tests.h"

namespace afc::editor {
LineWithCursor::Generator::Vector OutputFromRowsVector(RowsVector rows_vector) {
  LineWithCursor::Generator::Vector output;
  LineNumberDelta lines_to_skip;
  for (size_t row_index = 0; row_index < rows_vector.rows.size(); row_index++) {
    if (output.size() == rows_vector.lines) break;
    const auto& row = rows_vector.rows[row_index];
    CHECK_LT(output.size(), rows_vector.lines);
    LineNumberDelta lines_from_row = min(
        row.lines,
        rows_vector.lines +
            (row.overlap_behavior == RowsVector::Row::OverlapBehavior::kSolid
                 ? lines_to_skip
                 : LineNumberDelta()) -
            output.size());
    LineWithCursor::Generator::Vector row_output =
        row.callback == nullptr
            ? LineWithCursor::Generator::
                  Vector{.lines = std::vector<LineWithCursor::Generator>(
                             lines_from_row.line_delta,
                             LineWithCursor::Generator::Empty()),
                         .width = ColumnNumberDelta()}
            : row.callback(lines_from_row);
    row_output.lines.resize(lines_from_row.line_delta,
                            LineWithCursor::Generator::Empty());

    switch (row.overlap_behavior) {
      case RowsVector::Row::OverlapBehavior::kFloat:
        lines_to_skip += row_output.size();
        break;
      case RowsVector::Row::OverlapBehavior::kSolid:
        if (lines_to_skip >= row_output.size()) {
          lines_to_skip -= row_output.size();
          row_output.lines = {};
        } else {
          row_output.lines.erase(
              row_output.lines.begin(),
              row_output.lines.begin() + lines_to_skip.line_delta);
          lines_to_skip = LineNumberDelta();
        }
    }

    output.width = max(output.width, row_output.width);

    for (auto& generator : row_output.lines) {
      if (row_index != rows_vector.index_active) {
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
      output.lines.push_back(std::move(generator));
      if (output.size() == rows_vector.lines) break;
    }
  }

  std::vector<LineWithCursor::Generator> tail(
      (rows_vector.lines - output.size()).line_delta,
      LineWithCursor::Generator::Empty());
  output.lines.insert(output.lines.end(), tail.begin(), tail.end());
  return output;
}

namespace {
const bool tests_registration = tests::Register(L"OutputFromRowsVector", [] {
  auto Build = [](RowsVector rows_vector) {
    std::vector<std::wstring> output;
    for (auto& g : OutputFromRowsVector(rows_vector).lines)
      output.push_back(g.generate().line->ToString());
    return output;
  };
  return std::vector<tests::Test>{
      {.name = L"TwoRowsShort",
       .callback =
           [&] {
             RowsVector rows_vector;
             rows_vector.rows.push_back(
                 {.callback =
                      [](LineNumberDelta lines) {
                        return OutputProducer::Constant(
                                   LineWithCursor(Line(L"top")))
                            ->Produce(lines);
                      },
                  .lines = LineNumberDelta(2)});
             rows_vector.rows.push_back(
                 {.callback =
                      [](LineNumberDelta lines) {
                        return OutputProducer::Constant(
                                   LineWithCursor(Line(L"bottom")))
                            ->Produce(lines);
                      },
                  .lines = LineNumberDelta(2)});
             rows_vector.index_active = 0;
             rows_vector.lines = LineNumberDelta(20);
             auto output = Build(rows_vector);
             CHECK_EQ(output.size(), 20ul);
             CHECK(output[0] == L"top");
             CHECK(output[1] == L"top");
             CHECK(output[2] == L"bottom");
             CHECK(output[3] == L"bottom");
             CHECK(output[4] == L"");
             CHECK(output[5] == L"");
             CHECK(output[6] == L"");
             CHECK(output[7] == L"");
             CHECK(output[8] == L"");
             CHECK(output[9] == L"");
           }},
      {.name = L"FirstRowIsTooLong", .callback = [&] {
         RowsVector rows_vector;
         rows_vector.rows.push_back(
             {.callback =
                  [](LineNumberDelta lines_requested) {
                    CHECK_EQ(lines_requested, LineNumberDelta(2));
                    return RepeatLine(LineWithCursor(Line(L"top")),
                                      LineNumberDelta(10));
                  },
              .lines = LineNumberDelta(2)});
         rows_vector.rows.push_back({.callback =
                                         [](LineNumberDelta) {
                                           return RepeatLine(
                                               LineWithCursor(Line(L"bottom")),
                                               LineNumberDelta(10));
                                         },
                                     .lines = LineNumberDelta(10)});
         rows_vector.index_active = 0;
         rows_vector.lines = LineNumberDelta(5);
         auto output = Build(rows_vector);
         CHECK_EQ(output.size(), 5ul);
         CHECK(output[0] == L"top");
         CHECK(output[1] == L"top");
         CHECK(output[2] == L"bottom");
         CHECK(output[3] == L"bottom");
         CHECK(output[4] == L"bottom");
       }}};
}());

}  // namespace
}  // namespace afc::editor
