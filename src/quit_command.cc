#include <memory>

#include "command.h"
#include "editor.h"

namespace afc {
namespace editor {

namespace {

class QuitCommand : public Command {
 public:
  const wstring Description() { return L"quits"; }

  void ProcessInput(wint_t, EditorState* editor_state) {
    wstring error_description;
    if (!editor_state->AttemptTermination(&error_description)) {
      editor_state->SetWarningStatus(error_description);
    }
    if (editor_state->has_current_buffer()) {
      editor_state->current_buffer()->second->ResetMode();
    }
  }
};

}  // namespace

std::unique_ptr<Command> NewQuitCommand() {
  return std::make_unique<QuitCommand>();
}

}  // namespace editor
}  // namespace afc
