#include "close_buffer_command.h"

#include <memory>

#include "editor.h"

namespace afc {
namespace editor {

namespace {
class CloseBufferCommand : public Command {
  const wstring Description() { return L"closes the current buffer"; }

  void ProcessInput(wint_t, EditorState* editor_state) {
    if (!editor_state->has_current_buffer()) {
      return;
    }
    editor_state->CloseBuffer(editor_state->current_buffer());
    editor_state->ResetModifiers();
  }
};
}  // namespace

std::unique_ptr<Command> NewCloseBufferCommand() {
  return std::make_unique<CloseBufferCommand>();
}

}  // namespace editor
}  // namespace afc
