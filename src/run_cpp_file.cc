#include "src/run_cpp_file.h"

#include <memory>

#include "buffer.h"
#include "command.h"
#include "editor.h"
#include "file_link_mode.h"
#include "line_prompt_mode.h"

namespace afc {
namespace editor {

namespace {
class RunCppFileCommand : public Command {
 public:
  const wstring Description() {
    return L"runs a command from a file";
  }

  void ProcessInput(wint_t, EditorState* editor_state) {
    if (!editor_state->has_current_buffer()) { return; }
    auto buffer = editor_state->current_buffer()->second;
    PromptOptions options;
    options.prompt = L"cmd ";
    options.history_file = L"editor_commands";
    options.initial_value = buffer->read_string_variable(
        OpenBuffer::variable_editor_commands_path()),
    options.handler = RunCppFileHandler;
    options.cancel_handler = [](EditorState*) { /* Nothing. */ };
    options.predictor = FilePredictor;
    Prompt(editor_state, std::move(options));
  }
};
}  // namespace

void RunCppFileHandler(const wstring& input, EditorState* editor_state) {
  if (!editor_state->has_current_buffer()) { return; }
  OpenBuffer* buffer = editor_state->current_buffer()->second.get();
  if (editor_state->structure() == LINE) {
    auto target = buffer->GetBufferFromCurrentLine().get();
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
    editor_state->SetWarningStatus(L"File not found: " + input);
    return;
  }

  for (size_t i = 0; i < editor_state->repetitions(); i++) {
    buffer->EvaluateFile(editor_state, adjusted_input);
  }
  editor_state->ResetRepetitions();
}

std::unique_ptr<Command> NewRunCppFileCommand() {
  return std::unique_ptr<Command>(new RunCppFileCommand());
}

}  // namespace afc
}  // namespace editor
