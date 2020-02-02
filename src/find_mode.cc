#include "src/find_mode.h"

#include <list>
#include <memory>
#include <string>

#include "src/buffer_variables.h"
#include "src/command_mode.h"
#include "src/editor.h"
#include "src/transformation.h"
#include "src/transformation/composite.h"
#include "src/transformation/set_position.h"

namespace afc::editor {
namespace {
using std::shared_ptr;

class FindTransformation : public CompositeTransformation {
 public:
  FindTransformation(wchar_t c) : c_(c) {}

  std::wstring Serialize() const override { return L"FindTransformation();"; }
  futures::Value<Output> Apply(Input input) const override {
    auto line = input.buffer->LineAt(input.position.line);
    if (line == nullptr) return futures::Past(Output());
    ColumnNumber column = min(input.position.column, line->EndColumn());
    for (size_t i = 0; i < input.modifiers.repetitions; i++) {
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
  void ProcessInput(wint_t c, EditorState* editor_state) {
    editor_state->PushCurrentPosition();
    futures::ImmediateTransform(
        editor_state->ApplyToActiveBuffers(
            NewTransformation(editor_state->modifiers(),
                              std::make_unique<FindTransformation>(c))),
        [editor_state](bool) {
          editor_state->ResetRepetitions();
          editor_state->ResetDirection();
          editor_state->set_keyboard_redirect(nullptr);
          return true;
        });
  }
};

class FindModeCommand : public Command {
 public:
  wstring Description() const override {
    return L"Waits for a character to be typed and moves the cursor to its "
           L"next occurrence in the current line.";
  }
  wstring Category() const override { return L"Navigate"; }

  void ProcessInput(wint_t, EditorState* editor_state) {
    editor_state->set_keyboard_redirect(std::make_unique<FindMode>());
  }
};
}  // namespace

std::unique_ptr<Command> NewFindModeCommand() {
  return std::make_unique<FindModeCommand>();
}

}  // namespace afc::editor
