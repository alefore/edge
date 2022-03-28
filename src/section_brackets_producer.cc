#include "src/section_brackets_producer.h"

#include <glog/logging.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <iostream>

#include "src/char_buffer.h"
#include "src/lazy_string.h"
#include "src/line.h"

namespace afc::editor {
LineWithCursor::Generator::Vector SectionBrackets(
    LineNumberDelta lines, SectionBracketsSide section_brackets_side) {
  LineWithCursor::Generator::Vector output{.lines = {},
                                           .width = ColumnNumberDelta(1)};
  auto push = [&output](wstring c) {
    output.lines.push_back(LineWithCursor::Generator{
        std::hash<wstring>{}(c), [c]() {
          return LineWithCursor(Line(Line::Options(NewLazyString(c))));
        }});
  };
  push(section_brackets_side == SectionBracketsSide::kLeft ? L"╭" : L"╮");
  for (LineNumberDelta i(1); i + LineNumberDelta(1) < lines; ++i) push(L"│");
  push(section_brackets_side == SectionBracketsSide::kLeft ? L"╰" : L"╯");
  push(L"");
  return output;
}
}  // namespace afc::editor
