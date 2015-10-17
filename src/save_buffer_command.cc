#include <memory>

#include "command.h"
#include "editor.h"

namespace afc {
namespace editor {

namespace {

class SaveBufferCommand : public Command {
  const wstring Description() {
    return L"saves the current buffer";
  }

  void ProcessInput(wint_t, EditorState* editor_state) {
    if (!editor_state->has_current_buffer()) {
      return;
    }
    editor_state->current_buffer()->second->Save(editor_state);
    editor_state->set_structure(CHAR);
    editor_state->ResetRepetitions();
    editor_state->set_default_direction(FORWARDS);
    editor_state->ResetDirection();
    editor_state->ResetMode();
  }
};

}  // namespace

std::unique_ptr<Command> NewSaveBufferCommand() {
  return std::unique_ptr<Command>(new SaveBufferCommand());
}

}  // namespace afc
}  // namespace editor
