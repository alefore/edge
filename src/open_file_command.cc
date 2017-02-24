#include "open_file_command.h"

#include "editor.h"
#include "dirname.h"
#include "file_link_mode.h"
#include "line_prompt_mode.h"
#include "wstring.h"

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
          struct stat stat_buffer;
          if (stat(ToByteString(path).c_str(), &stat_buffer) == -1
              || !S_ISDIR(stat_buffer.st_mode)) {
            LOG(INFO) << "Taking dirname for prompt: " << path;
            path = Dirname(path);
          }
          if (path == L".") {
            path = L"";
          } else if (path.empty() || *path.rbegin() != L'/') {
            path += L'/';
          }
          options_copy.initial_value = path;
        }
        return options_copy;
      });
}

}  // namespace afc
}  // namespace editor
