#include "src/horizontal_split_output_producer.h"

#include <glog/logging.h>

#include <algorithm>
#include <cctype>
#include <iostream>

#include "src/tests/tests.h"

namespace afc::editor {
std::vector<OutputProducer::Generator> HorizontalSplitOutputProducer::Generate(
    LineNumberDelta lines) {
  std::vector<Generator> output;
  auto output_size = [&output] { return LineNumberDelta(output.size()); };
  LineNumberDelta lines_to_skip;
  for (size_t row_index = 0; row_index < rows_.size(); row_index++) {
    if (output_size() == lines) break;
    const auto& row = rows_[row_index];
    CHECK_LT(output_size(), lines);
    LineNumberDelta lines_from_row =
        min(row.lines, lines +
                           (row.overlap_behavior == Row::OverlapBehavior::kSolid
                                ? lines_to_skip
                                : LineNumberDelta()) -
                           output_size());
    std::vector<Generator> row_generators =
        row.producer == nullptr
            ? std::vector<Generator>(lines_from_row.line_delta,
                                     Generator::Empty())
            : row.producer->Generate(lines_from_row);
    switch (row.overlap_behavior) {
      case Row::OverlapBehavior::kFloat:
        lines_to_skip += LineNumberDelta(row_generators.size());
        break;
      case Row::OverlapBehavior::kSolid:
        if (lines_to_skip >= LineNumberDelta(row_generators.size())) {
          lines_to_skip -= LineNumberDelta(row_generators.size());
          row_generators = {};
        } else {
          row_generators.erase(
              row_generators.begin(),
              row_generators.begin() + lines_to_skip.line_delta);
          lines_to_skip = LineNumberDelta();
        }
    }
    for (auto& generator : row_generators) {
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
      output.push_back(std::move(generator));
      if (output_size() == lines) break;
    }
  }

  std::vector<Generator> tail((lines - output_size()).line_delta,
                              Generator::Empty());
  output.insert(output.end(), tail.begin(), tail.end());
  return output;
}

namespace {
using H = HorizontalSplitOutputProducer;
const bool tests_registration =
    tests::Register(L"HorizontalSplitOutputProducer", [] {
      auto Build = [](H producer, LineNumberDelta lines) {
        std::vector<std::wstring> output;
        for (auto& g : producer.Generate(lines))
          output.push_back(g.generate().line->ToString());
        return output;
      };
      return std::vector<tests::Test>{
          {.name = L"TwoRowsShort", .callback = [&] {
             std::vector<H::Row> rows;
             rows.push_back(
                 {.producer = OutputProducer::Constant(H::LineWithCursor{
                      .line = std::make_shared<Line>(L"top")}),
                  .lines = LineNumberDelta(2)});
             rows.push_back(
                 {.producer = OutputProducer::Constant(H::LineWithCursor{
                      .line = std::make_shared<Line>(L"bottom")}),
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
