#include "src/horizontal_split_output_producer.h"

#include <glog/logging.h>

#include <algorithm>
#include <cctype>
#include <iostream>

#include "src/tests/tests.h"

namespace afc::editor {
OutputProducer::Output HorizontalSplitOutputProducer::Produce(
    LineNumberDelta lines) {
  Output output;
  LineNumberDelta lines_to_skip;
  for (size_t row_index = 0; row_index < rows_.size(); row_index++) {
    if (output.size() == lines) break;
    const auto& row = rows_[row_index];
    CHECK_LT(output.size(), lines);
    LineNumberDelta lines_from_row =
        min(row.lines, lines +
                           (row.overlap_behavior == Row::OverlapBehavior::kSolid
                                ? lines_to_skip
                                : LineNumberDelta()) -
                           output.size());
    Output row_output =
        row.callback == nullptr
            ? Output{.lines = std::vector<Generator>(lines_from_row.line_delta,
                                                     Generator::Empty()),
                     .width = ColumnNumberDelta()}
            : row.callback(lines_from_row);

    switch (row.overlap_behavior) {
      case Row::OverlapBehavior::kFloat:
        lines_to_skip += row_output.size();
        break;
      case Row::OverlapBehavior::kSolid:
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
      if (row_index != index_active_) {
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
      if (output.size() == lines) break;
    }
  }

  std::vector<Generator> tail((lines - output.size()).line_delta,
                              Generator::Empty());
  output.lines.insert(output.lines.end(), tail.begin(), tail.end());
  return output;
}

namespace {
using H = HorizontalSplitOutputProducer;
const bool tests_registration =
    tests::Register(L"HorizontalSplitOutputProducer", [] {
      auto Build = [](H producer, LineNumberDelta lines) {
        std::vector<std::wstring> output;
        for (auto& g : producer.Produce(lines).lines)
          output.push_back(g.generate().line->ToString());
        return output;
      };
      return std::vector<tests::Test>{
          {.name = L"TwoRowsShort", .callback = [&] {
             std::vector<H::Row> rows;
             rows.push_back({.callback =
                                 [](LineNumberDelta lines) {
                                   return OutputProducer::Constant(
                                              LineWithCursor(Line(L"top")))
                                       ->Produce(lines);
                                 },
                             .lines = LineNumberDelta(2)});
             rows.push_back({.callback =
                                 [](LineNumberDelta lines) {
                                   return OutputProducer::Constant(
                                              LineWithCursor(Line(L"bottom")))
                                       ->Produce(lines);
                                 },
                             .lines = LineNumberDelta(2)});
             auto output = Build(H(std::move(rows), 0), LineNumberDelta(10));
             CHECK_EQ(output.size(), 10ul);
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
           }}};
    }());

}  // namespace
}  // namespace afc::editor
