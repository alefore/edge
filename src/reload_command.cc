#include "src/reload_command.h"

#include "command.h"
#include "editor.h"
#include "file_link_mode.h"
#include "line_prompt_mode.h"
#include "wstring.h"

namespace afc {
namespace editor {

namespace {

class ReloadCommand : public Command {
 public:
  const wstring Description() {
    return L"reloads the current buffer";
  }

  void ProcessInput(wint_t, EditorState* editor_state) {
    switch (editor_state->structure()) {
      case LINE:
        if (editor_state->has_current_buffer()) {
          auto buffer = editor_state->current_buffer()->second;
          if (buffer->current_line() != nullptr &&
              buffer->current_line()->activate() != nullptr) {
            buffer->current_line()->activate()->ProcessInput('r', editor_state);
          }
        }
        break;
      default:
        if (editor_state->has_current_buffer()) {
          auto buffer = editor_state->current_buffer();
          buffer->second->Reload(editor_state);
        }
    }
    editor_state->ResetMode();
    editor_state->ResetRepetitions();
    editor_state->ResetStructure();
  }
};

}  // namespace

std::unique_ptr<Command> NewReloadCommand() {
  return std::unique_ptr<Command>(new ReloadCommand());
}

}  // namespace afc
}  // namespace editor
