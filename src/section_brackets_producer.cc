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
SectionBracketsProducer::SectionBracketsProducer(LineNumberDelta lines)
    : lines_(lines) {}

SectionBracketsProducer::Generator SectionBracketsProducer::Next() {
  wstring c;
  if (current_line_ == LineNumber(0)) {
    c = L"╭";
  } else if ((current_line_ + LineNumberDelta(1)).ToDelta() == lines_) {
    c = L"╰";
  } else {
    c = L"│";
  }
  ++current_line_;
  return Generator{std::hash<wstring>{}(c), [c]() {
                     return LineWithCursor{std::make_shared<Line>(
                                               Line::Options(NewLazyString(c))),
                                           std::nullopt};
                   }};
}
}  // namespace afc::editor
