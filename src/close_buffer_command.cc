#include "close_buffer_command.h"

#include <memory>

#include "editor.h"

namespace afc {
namespace editor {

namespace {

class CloseBufferCommand : public Command {
  const wstring Description() {
    return L"closes the current buffer";
  }

  void ProcessInput(wint_t, EditorState* editor_state) {
    if (!editor_state->has_current_buffer()) {
      return;
    }
    editor_state->CloseBuffer(editor_state->current_buffer());
    editor_state->set_structure(CHAR);
    editor_state->set_sticky_structure(false);
    editor_state->ResetRepetitions();
    editor_state->set_default_direction(FORWARDS);
    editor_state->ResetDirection();
    editor_state->ResetMode();
  }
};

}  // namespace

std::unique_ptr<Command> NewCloseBufferCommand() {
  return unique_ptr<Command>(new CloseBufferCommand());
}

}  // namespace afc
}  // namespace editor
