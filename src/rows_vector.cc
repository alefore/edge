#include "src/rows_vector.h"

#include <glog/logging.h>

#include <algorithm>
#include <cctype>
#include <iostream>

#include "src/tests/tests.h"

namespace afc::editor {
LineWithCursor::Generator::Vector AppendRows(
    LineWithCursor::Generator::Vector head,
    LineWithCursor::Generator::Vector tail, size_t index_active) {
  CHECK(index_active == 0 || index_active == 1);
  for (auto& generator : (index_active == 0 ? tail : head).lines) {
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
  head.lines.insert(head.lines.end(), tail.lines.begin(), tail.lines.end());
  return head;
}

namespace {
const bool tests_registration = tests::Register(L"AppendRows", [] {
  auto Build = [](LineWithCursor::Generator::Vector rows) {
    std::vector<std::wstring> output;
    for (auto& g : rows.lines) output.push_back(g.generate().line->ToString());
    return output;
  };
  return std::vector<tests::Test>{
      {.name = L"TwoRowsShort", .callback = [&] {
         auto output = Build(AppendRows(
             RepeatLine(LineWithCursor(Line(L"top")), LineNumberDelta(2)),
             RepeatLine(LineWithCursor(Line(L"bottom")), LineNumberDelta(2)),
             0));
         CHECK_EQ(output.size(), 4ul);
         CHECK(output[0] == L"top");
         CHECK(output[1] == L"top");
         CHECK(output[2] == L"bottom");
         CHECK(output[3] == L"bottom");
       }}};
}());

}  // namespace
}  // namespace afc::editor
