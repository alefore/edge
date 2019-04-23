#include "src/run_cpp_command.h"

#include <memory>

#include "src/buffer.h"
#include "src/command.h"
#include "src/editor.h"
#include "src/line_prompt_mode.h"

namespace afc {
namespace editor {

namespace {

void RunCppCommandHandler(const wstring& name, EditorState* editor_state) {
  auto buffer = editor_state->current_buffer();
  if (buffer == nullptr) {
    return;
  }
  buffer->ResetMode();
  buffer->EvaluateString(name, [](std::unique_ptr<Value>) { /* Nothing. */ });
}

class RunCppCommand : public Command {
 public:
  wstring Description() const override {
    return L"prompts for a command (a C string) and runs it";
  }
  wstring Category() const override { return L"Extensions"; }

  void ProcessInput(wint_t, EditorState* editor_state) override {
    auto buffer = editor_state->current_buffer();
    if (buffer == nullptr) {
      return;
    }
    if (editor_state->structure() == StructureLine()) {
      editor_state->ResetStructure();
      RunCppCommandHandler(buffer->current_line()->ToString(), editor_state);
    } else {
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

}  // namespace editor
}  // namespace afc
