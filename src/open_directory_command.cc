#include "open_directory_command.h"

extern "C" {
#include <libgen.h>
}

#include "command.h"
#include "editor.h"
#include "file_link_mode.h"
#include "wstring.h"

namespace afc {
namespace editor {

namespace {

class OpenDirectoryCommand : public Command {
  const wstring Description() {
    return L"opens a view of the current directory";
  }

  void ProcessInput(wint_t, EditorState* editor_state) {
    wstring path;
    if (!editor_state->has_current_buffer()) {
      path = L".";
    } else {
      // TODO: We could alter ToByteString to return a char* and avoid the extra
      // copy.
      char* tmp = strdup(
          ToByteString(editor_state->current_buffer()->first.c_str()).c_str());
      path = FromByteString(dirname(tmp));
      free(tmp);
    }
    OpenFileOptions options;
    options.editor_state = editor_state;
    options.path = path;
    OpenFile(options);
    editor_state->ResetMode();
  }
};

}  // namespace

std::unique_ptr<Command> NewOpenDirectoryCommand() {
  return std::unique_ptr<Command>(new OpenDirectoryCommand());
}

}  // namespace afc
}  // namespace editor
