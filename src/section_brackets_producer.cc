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
OutputProducer::Output SectionBracketsProducer::Produce(LineNumberDelta lines) {
  Output output{.lines = {}, .width = ColumnNumberDelta(1)};
  auto push = [&output](wstring c) {
    output.lines.push_back(Generator{
        std::hash<wstring>{}(c), [c]() {
          return LineWithCursor(Line(Line::Options(NewLazyString(c))));
        }});
  };
  push(L"╭");
  for (LineNumberDelta i(1); i + LineNumberDelta(1) < lines; ++i) push(L"│");
  push(L"╰");
  return output;
}
}  // namespace afc::editor
