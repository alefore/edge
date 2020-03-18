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
namespace {
class FindTransformation : public CompositeTransformation {
 public:
  FindTransformation(wchar_t c) : c_(c) {}

  std::wstring Serialize() const override { return L"FindTransformation();"; }
  futures::Value<Output> Apply(Input input) const override {
    auto line = input.buffer->LineAt(input.position.line);
    if (line == nullptr) return futures::Past(Output());
    ColumnNumber column = min(input.position.column, line->EndColumn());
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

  std::unique_ptr<CompositeTransformation> Clone() const override {
    return std::make_unique<FindTransformation>(c_);
  }

 private:
  std::optional<ColumnNumber> SeekOnce(const Line& line, ColumnNumber column,
                                       const Modifiers& modifiers) const {
    ColumnNumberDelta direction;
    ColumnNumberDelta times;
    switch (modifiers.direction) {
      case Direction::kForwards:
        direction = ColumnNumberDelta(1);
        times = line.EndColumn() - column;
        break;
      case Direction::kBackwards:
        direction = ColumnNumberDelta(-1);
        times = (column + ColumnNumberDelta(1)).ToDelta();
        break;
    }

    CHECK_GE(times, ColumnNumberDelta(0));
    for (size_t i = 1; i < static_cast<size_t>(times.column_delta); i++) {
      if (line.get(column + direction * i) == static_cast<wint_t>(c_)) {
        return column + direction * i;
      }
    }
    return std::nullopt;
  }

  const wchar_t c_;
};

class FindMode : public EditorMode {
 public:
  FindMode(Direction initial_direction)
      : initial_direction_(initial_direction) {}

  void ProcessInput(wint_t c, EditorState* editor_state) override {
    editor_state->PushCurrentPosition();
    switch (initial_direction_) {
      case Direction::kBackwards:
        editor_state->set_direction(
            ReverseDirection(editor_state->direction()));
        break;
      case Direction::kForwards:
        break;
    }
    futures::Transform(editor_state->ApplyToActiveBuffers(
                           transformation::ModifiersAndComposite{
                               editor_state->modifiers(),
                               std::make_unique<FindTransformation>(c)}),
                       [editor_state](EmptyValue) {
                         editor_state->ResetRepetitions();
                         editor_state->ResetDirection();
                         editor_state->set_keyboard_redirect(nullptr);
                         return futures::Past(EmptyValue());
                       });
  }

 private:
  const Direction initial_direction_;
};
}  // namespace

std::unique_ptr<Command> NewFindModeCommand(Direction initial_direction) {
  return NewSetModeCommand(
      {.description =
           L"Waits for a character to be typed and moves the cursor to its "
           L"next occurrence in the current line.",
       .category = L"Navigate",
       .factory = [initial_direction] {
         return std::make_unique<FindMode>(initial_direction);
       }});
}

}  // namespace afc::editor
