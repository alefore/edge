#include "src/section_brackets_producer.h"

#include <glog/logging.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <iostream>

#include "src/language/lazy_string/char_buffer.h"
#include "src/language/lazy_string/lazy_string.h"
#include "src/line.h"
#include "src/tests/tests.h"

namespace afc::editor {
using language::MakeNonNullShared;
using language::lazy_string::ColumnNumberDelta;
using language::lazy_string::NewLazyString;

LineWithCursor::Generator::Vector SectionBrackets(
    LineNumberDelta lines, SectionBracketsSide section_brackets_side) {
  LineWithCursor::Generator::Vector output{.lines = {},
                                           .width = ColumnNumberDelta(1)};
  auto push = [&output, lines](std::wstring c) {
    if (output.size() < lines)
      output.lines.push_back(LineWithCursor::Generator{
          std::hash<std::wstring>{}(c), [c]() {
            return LineWithCursor{
                .line = MakeNonNullShared<Line>(LineBuilder(NewLazyString(c)))};
          }});
  };
  push(section_brackets_side == SectionBracketsSide::kLeft ? L"╭" : L"╮");
  for (LineNumberDelta i(1); i + LineNumberDelta(1) < lines; ++i) push(L"│");
  push(section_brackets_side == SectionBracketsSide::kLeft ? L"╰" : L"╯");
  push(L"");
  return output;
}

namespace {
const bool tests_registration = tests::Register(
    L"SectionBrackets",
    {
        {.name = L"Empty",
         .callback =
             [] {
               CHECK_EQ(SectionBrackets(LineNumberDelta(0),
                                        SectionBracketsSide::kLeft)
                            .size(),
                        LineNumberDelta(0));
             }},
        {.name = L"Short",
         .callback =
             [] {
               CHECK_EQ(SectionBrackets(LineNumberDelta(1),
                                        SectionBracketsSide::kLeft)
                            .size(),
                        LineNumberDelta(1));
               CHECK_EQ(SectionBrackets(LineNumberDelta(2),
                                        SectionBracketsSide::kLeft)
                            .size(),
                        LineNumberDelta(2));
               CHECK_EQ(SectionBrackets(LineNumberDelta(3),
                                        SectionBracketsSide::kLeft)
                            .size(),
                        LineNumberDelta(3));
             }},
        {.name = L"BasicCall",
         .callback =
             [] {
               auto output = SectionBrackets(LineNumberDelta(10),
                                             SectionBracketsSide::kLeft);
               CHECK_EQ(output.size(), LineNumberDelta(10));
               CHECK_EQ(output.width, ColumnNumberDelta(1));
             }},
    });
}
}  // namespace afc::editor
