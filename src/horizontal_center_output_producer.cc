#include "src/horizontal_center_output_producer.h"

#include <glog/logging.h>

#include <algorithm>
#include <cctype>
#include <iostream>

#include "src/columns_vector.h"
#include "src/language/container.h"
#include "src/language/lazy_string/char_buffer.h"

namespace container = afc::language::container;
using afc::infrastructure::screen::LineModifier;
using afc::infrastructure::screen::LineModifierSet;
using afc::language::lazy_string::ColumnNumberDelta;
using afc::language::lazy_string::LazyString;
using afc::language::lazy_string::NewLazyString;

namespace afc::editor {
using V = ColumnsVector;
LineWithCursor::Generator::Vector CenterOutput(
    LineWithCursor::Generator::Vector lines, ColumnNumberDelta width,
    std::vector<LineModifier> padding_modifiers) {
  if (lines.width >= width) return lines;

  ColumnsVector columns_vector{.index_active = 1};
  columns_vector.push_back(
      V::Column{.lines = {}, .width = (width - lines.width) / 2});

  columns_vector.push_back(
      V::Column{.lines = lines,
                .padding = container::MaterializeVector(
                    padding_modifiers |
                    std::views::transform(
                        [](LineModifier m) -> std::optional<V::Padding> {
                          return V::Padding{.modifiers = LineModifierSet{m},
                                            .head = LazyString(),
                                            .body = NewLazyString(L"█")};
                        })),
                .width = lines.width});
  return OutputFromColumnsVector(std::move(columns_vector));
}

}  // namespace afc::editor
