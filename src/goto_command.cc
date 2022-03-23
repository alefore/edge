#include "src/goto_command.h"

#include <glog/logging.h>

#include <cmath>

#include "src/buffer.h"
#include "src/buffer_variables.h"
#include "src/command.h"
#include "src/command_argument_mode.h"
#include "src/editor.h"
#include "src/futures/futures.h"
#include "src/lazy_string_functional.h"
#include "src/transformation.h"
#include "src/transformation/composite.h"
#include "src/transformation/set_position.h"

namespace afc {
namespace editor {

namespace {
// Arguments:
//   prefix_len: The length of prefix that we skip when calls is 0.
//   suffix_start: The position where the suffix starts. This is the base when
//       calls is 2.
//   elements: The total number of elements.
//   direction: The direction of movement.
//   repetitions: The nth element to jump to.
//   structure_range: The StructureRange. If FROM_CURRENT_POSITION_TO_END, it
//       reverses the direction.
//   calls: The number of consecutive number of times this command has run.
size_t ComputePosition(size_t prefix_len, size_t suffix_start, size_t elements,
                       Direction direction, size_t repetitions, size_t calls) {
  CHECK_LE(prefix_len, suffix_start);
  CHECK_LE(suffix_start, elements);
  if (calls > 1) {
    return ComputePosition(prefix_len, suffix_start, elements,
                           ReverseDirection(direction), repetitions, calls - 2);
  }
  if (calls == 1) {
    return ComputePosition(0, elements, elements, direction, repetitions, 0);
  }

  switch (direction) {
    case Direction::kForwards:
      return min(prefix_len + repetitions - 1, elements);
    case Direction::kBackwards:
      return suffix_start - min(suffix_start, repetitions - 1);
  }
  LOG(FATAL) << "Invalid direction.";
  return 0;
}

class GotoCommand : public Command {
 public:
  GotoCommand(EditorState& editor_state, size_t calls)
      : editor_state_(editor_state), calls_(calls % 4) {}

  wstring Description() const override {
    return L"goes to Rth structure from the beginning";
  }
  wstring Category() const override { return L"Navigate"; }

  void ProcessInput(wint_t c) {
    if (c != 'g') {
      auto old_mode = editor_state_.set_keyboard_redirect(nullptr);
      editor_state_.ProcessInput(c);
      return;
    }
    auto structure = editor_state_.structure();
    if (structure == StructureChar() || structure == StructureSymbol() ||
        structure == StructureLine() || structure == StructureMark() ||
        structure == StructurePage() || structure == StructureSearch() ||
        structure == StructureCursor()) {
      editor_state_.ApplyToActiveBuffers(
          std::make_unique<GotoTransformation>(calls_));
    } else if (structure == StructureBuffer()) {
      size_t buffers = editor_state_.buffers()->size();
      size_t position =
          ComputePosition(0, buffers, buffers, editor_state_.direction(),
                          editor_state_.repetitions().value_or(1), calls_);
      CHECK_LT(position, editor_state_.buffers()->size());
      auto it = editor_state_.buffers()->begin();
      advance(it, position);
      if (it->second != editor_state_.current_buffer()) {
        editor_state_.set_current_buffer(it->second,
                                         CommandArgumentModeApplyMode::kFinal);
      }
    }

    editor_state_.PushCurrentPosition();
    editor_state_.ResetStructure();
    editor_state_.ResetDirection();
    editor_state_.ResetRepetitions();
    editor_state_.set_keyboard_redirect(
        std::make_unique<GotoCommand>(editor_state_, calls_ + 1));
  }

 private:
  EditorState& editor_state_;
  const size_t calls_;
};

}  // namespace

GotoTransformation::GotoTransformation(int calls) : calls_(calls) {}

std::wstring GotoTransformation::Serialize() const {
  return L"GotoTransformation()";
}

futures::Value<CompositeTransformation::Output> GotoTransformation::Apply(
    CompositeTransformation::Input input) const {
  static Tracker tracker(L"GotoTransformation::Apply");
  auto call = tracker.Call();
  auto position = input.modifiers.structure->ComputeGoToPosition(
      input.buffer, input.modifiers, input.position, calls_);
  return futures::Past(
      position.has_value() ? Output::SetPosition(position.value()) : Output());
}

std::unique_ptr<CompositeTransformation> GotoTransformation::Clone() const {
  return std::make_unique<GotoTransformation>(calls_);
}

std::unique_ptr<Command> NewGotoCommand(EditorState& editor_state) {
  return std::make_unique<GotoCommand>(editor_state, 0);
}

}  // namespace editor
}  // namespace afc
