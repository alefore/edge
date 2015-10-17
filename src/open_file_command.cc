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
  return NewLinePromptCommand(
      L"<", L"files", L"loads a file", OpenFileHandler, FilePredictor);
}

}  // namespace afc
}  // namespace editor
