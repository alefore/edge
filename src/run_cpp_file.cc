#include "src/run_cpp_file.h"

#include <memory>

#include "src/buffer.h"
#include "src/buffer_variables.h"
#include "src/command.h"
#include "src/editor.h"
#include "src/file_link_mode.h"
#include "src/line_prompt_mode.h"

namespace afc {
namespace editor {

namespace {
class RunCppFileCommand : public Command {
 public:
  wstring Description() const override { return L"runs a command from a file"; }
  wstring Category() const override { return L"Extensions"; }

  void ProcessInput(wint_t, EditorState* editor_state) override {
    if (!editor_state->has_current_buffer()) {
      return;
    }
    auto buffer = editor_state->current_buffer();
    PromptOptions options;
    options.prompt = L"cmd ";
    options.history_file = L"editor_commands";
    options.initial_value =
        buffer->Read(buffer_variables::editor_commands_path());
    options.handler = RunCppFileHandler;
    options.cancel_handler = [](EditorState*) { /* Nothing. */ };
    options.predictor = FilePredictor;
    Prompt(editor_state, std::move(options));
  }
};
}  // namespace

void RunCppFileHandler(const wstring& input, EditorState* editor_state) {
  auto buffer = editor_state->current_buffer();
  if (buffer == nullptr) {
    return;
  }
  if (editor_state->structure() == StructureLine()) {
    auto target = buffer->GetBufferFromCurrentLine();
    if (target != nullptr) {
      buffer = target;
    }
    editor_state->ResetModifiers();
  }

  buffer->ResetMode();
  wstring adjusted_input;
  ResolvePathOptions options;
  options.editor_state = editor_state;
  options.path = input;
  options.output_path = &adjusted_input;
  if (!ResolvePath(options)) {
    editor_state->SetWarningStatus(L"🗱  File not found: " + input);
    return;
  }

  // Recursive function that receives the number of evaluations.
  auto execute = std::make_shared<std::function<void(size_t)>>();
  *execute = [buffer, total = editor_state->repetitions(), adjusted_input,
              execute](size_t i) {
    if (i >= total) return;
    buffer->EvaluateFile(adjusted_input, [execute, i](std::unique_ptr<Value>) {
      (*execute)(i + 1);
    });
  };
  (*execute)(0);

  editor_state->ResetRepetitions();
}

std::unique_ptr<Command> NewRunCppFileCommand() {
  return std::make_unique<RunCppFileCommand>();
}

}  // namespace editor
}  // namespace afc
