#include "src/horizontal_center_output_producer.h"

#include <glog/logging.h>

#include <algorithm>
#include <cctype>
#include <iostream>

#include "src/columns_vector.h"
#include "src/language/container.h"
#include "src/language/lazy_string/char_buffer.h"

namespace container = afc::language::container;
using afc::infrastructure::screen::Color;
using afc::infrastructure::screen::Style;
using afc::infrastructure::screen::StyleAttribute;
using afc::language::lazy_string::ColumnNumberDelta;
using afc::language::lazy_string::LazyString;
using afc::language::lazy_string::SingleLine;

namespace afc::editor {
using V = ColumnsVector;
LineWithCursor::Generator::Vector CenterOutput(
    LineWithCursor::Generator::Vector lines, ColumnNumberDelta width,
    std::vector<Style> padding_modifiers) {
  if (lines.width >= width) return lines;

  ColumnsVector columns_vector{.index_active = 1};
  columns_vector.push_back(
      V::Column{.lines = {}, .width = (width - lines.width) / 2});

  columns_vector.push_back(V::Column{
      .lines = lines,
      .padding =
          padding_modifiers |
          std::views::transform([](Style style) -> std::optional<V::Padding> {
            return V::Padding{.modifiers = style,
                              .head = SingleLine{},
                              .body = SingleLine::Char<L'█'>()};
          }) |
          std::ranges::to<std::vector>(),
      .width = lines.width});
  return OutputFromColumnsVector(std::move(columns_vector));
}

}  // namespace afc::editor
