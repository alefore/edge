#include "src/find_mode.h"

#include <list>
#include <memory>
#include <string>

#include "src/buffer_variables.h"
#include "src/command_mode.h"
#include "src/editor.h"
#include "src/set_mode_command.h"
#include "src/transformation.h"
#include "src/transformation/composite.h"
#include "src/transformation/set_position.h"
#include "src/transformation/stack.h"

namespace afc::editor {
using language::MakeNonNullUnique;
using language::NonNull;

FindTransformation::FindTransformation(wchar_t c) : c_(c) {}

std::wstring FindTransformation::Serialize() const {
  return L"FindTransformation();";
}

futures::Value<CompositeTransformation::Output> FindTransformation::Apply(
    CompositeTransformation::Input input) const {
  auto position = input.buffer.AdjustLineColumn(input.position);
  auto line = input.buffer.LineAt(position.line);
  if (line == nullptr) return futures::Past(Output());
  ColumnNumber column = std::min(position.column, line->EndColumn());
  for (size_t i = 0; i < input.modifiers.repetitions.value_or(1); i++) {
    auto candidate = SeekOnce(*line, column, input.modifiers);
    if (!candidate.has_value()) break;
    column = candidate.value();
  }
  if (column == input.position.column) {
    return futures::Past(Output());
  }
  return futures::Past(Output::SetColumn(column));
}

std::optional<ColumnNumber> FindTransformation::SeekOnce(
    const Line& line, ColumnNumber column, const Modifiers& modifiers) const {
  int direction;
  ColumnNumberDelta times;
  switch (modifiers.direction) {
    case Direction::kForwards:
      direction = 1;
      times = line.EndColumn() - column;
      break;
    case Direction::kBackwards:
      direction = -1;
      times = (column + ColumnNumberDelta(1)).ToDelta();
      break;
  }

  CHECK_GE(times, ColumnNumberDelta(0));
  ColumnNumberDelta start(0);

  // Seek until we're at a different character:
  while (start < times && column + direction * start < line.EndColumn() &&
         line.get(column + direction * start) == static_cast<wint_t>(c_))
    ++start;

  while (start < times) {
    if (column + direction * start < line.EndColumn() &&
        line.get(column + direction * start) == static_cast<wint_t>(c_)) {
      return column + direction * start;
    }
    ++start;
  }
  return std::nullopt;
}

}  // namespace afc::editor
