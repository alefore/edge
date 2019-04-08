#include "open_directory_command.h"

extern "C" {
#include <libgen.h>
}

#include "command.h"
#include "dirname.h"
#include "editor.h"
#include "file_link_mode.h"
#include "wstring.h"

namespace afc {
namespace editor {

namespace {

class OpenDirectoryCommand : public Command {
  wstring Description() const override {
    return L"opens a view of the current directory";
  }
  wstring Category() const override { return L"Buffers"; }

  void ProcessInput(wint_t, EditorState* editor_state) override {
    OpenFileOptions options;
    if (!editor_state->has_current_buffer()) {
      options.path = L".";
    } else {
      options.path = Dirname(editor_state->current_buffer()->first);
    }
    options.editor_state = editor_state;
    OpenFile(options);
  }
};

}  // namespace

std::unique_ptr<Command> NewOpenDirectoryCommand() {
  return std::make_unique<OpenDirectoryCommand>();
}

}  // namespace editor
}  // namespace afc
