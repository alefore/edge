#include "src/horizontal_center_output_producer.h"

#include <glog/logging.h>

#include <algorithm>
#include <cctype>
#include <iostream>

#include "src/columns_vector.h"
#include "src/language/lazy_string/char_buffer.h"

namespace afc::editor {
using language::lazy_string::ColumnNumberDelta;
using language::lazy_string::EmptyString;
using language::lazy_string::NewLazyString;

using V = ColumnsVector;
LineWithCursor::Generator::Vector CenterOutput(
    LineWithCursor::Generator::Vector lines, ColumnNumberDelta width,
    std::vector<LineModifier> padding_modifiers) {
  if (lines.width >= width) return lines;

  ColumnsVector columns_vector{.index_active = 1};

  columns_vector.push_back(
      V::Column{.lines = {}, .width = (width - lines.width) / 2});
  std::vector<std::optional<V::Padding>> padding;
  for (LineModifier m : padding_modifiers)
    padding.push_back(V::Padding{.modifiers = LineModifierSet{m},
                                 .head = EmptyString(),
                                 .body = NewLazyString(L"█")});
  columns_vector.push_back(V::Column{
      .lines = lines, .padding = std::move(padding), .width = lines.width});
  return OutputFromColumnsVector(std::move(columns_vector));
}

}  // namespace afc::editor
