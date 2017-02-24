#include "open_file_command.h"

#include "editor.h"
#include "file_link_mode.h"
#include "line_prompt_mode.h"

namespace afc {
namespace editor {

namespace {

void OpenFileHandler(const wstring& name, EditorState* editor_state) {
  OpenFileOptions options;
  options.editor_state = editor_state;
  options.path = name;
  OpenFile(options);
  editor_state->ResetMode();
}

}  // namespace

std::unique_ptr<Command> NewOpenFileCommand() {
  PromptOptions options;
  options.prompt = L"<";
  options.history_file = L"files";
  options.handler = OpenFileHandler;
  options.cancel_handler = [](EditorState* editor_state) {
    editor_state->ResetMode();
  };
  options.predictor = FilePredictor;
  return NewLinePromptCommand(
      L"loads a file",
      [options](EditorState* editor_state) {
        PromptOptions options_copy = options;
        if (editor_state->has_current_buffer()) {
          wstring path =
              editor_state->current_buffer()->second->read_string_variable(
                  OpenBuffer::variable_path());
          options_copy.initial_value = path;
        }
        return options_copy;
      });
}

}  // namespace afc
}  // namespace editor
