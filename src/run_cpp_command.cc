#include "src/run_cpp_command.h"

#include <memory>

#include "buffer.h"
#include "command.h"
#include "editor.h"
#include "line_prompt_mode.h"

namespace afc {
namespace editor {

namespace {

void RunCppCommandHandler(const wstring& name, EditorState* editor_state) {
  if (!editor_state->has_current_buffer()) { return; }
  editor_state->current_buffer()->second->ResetMode();
  editor_state->current_buffer()->second->EvaluateString(
      editor_state, name, [](std::unique_ptr<Value>) { /* Nothing. */ });
}

class RunCppCommand : public Command {
 public:
  const wstring Description() {
    return L"prompts for a command (a C string) and runs it";
  }

  void ProcessInput(wint_t, EditorState* editor_state) {
    if (!editor_state->has_current_buffer()) { return; }
    switch (editor_state->structure()) {
      case LINE:
        editor_state->ResetStructure();
        RunCppCommandHandler(
            editor_state->current_buffer()->second->current_line()->ToString(),
            editor_state);
        break;

      default:
        PromptOptions options;
        options.prompt = L"cpp ";
        options.history_file = L"cpp";
        options.handler = RunCppCommandHandler;
        options.cancel_handler = [](EditorState*) { /* Nothing. */ };
        Prompt(editor_state, options);
    }
  }
};

}  // namespace

std::unique_ptr<Command> NewRunCppCommand() {
  return std::make_unique<RunCppCommand>();
}

}  // namespace afc
}  // namespace editor
