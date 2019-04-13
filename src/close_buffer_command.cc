#include "src/close_buffer_command.h"

#include <memory>

#include "src/editor.h"

namespace afc {
namespace editor {

namespace {
class CloseBufferCommand : public Command {
  wstring Description() const override { return L"closes the current buffer"; }
  wstring Category() const override { return L"Buffers"; }

  void ProcessInput(wint_t, EditorState* editor_state) override {
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
