#include "src/open_directory_command.h"

extern "C" {
#include <libgen.h>
}

#include "src/buffer_variables.h"
#include "src/command.h"
#include "src/dirname.h"
#include "src/editor.h"
#include "src/file_link_mode.h"
#include "src/wstring.h"

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
    auto buffer = editor_state->current_buffer();
    if (buffer == nullptr) {
      options.path = Path::LocalDirectory();
    } else if (auto path =
                   Path::FromString(buffer->Read(buffer_variables::name));
               !path.IsError()) {
      if (auto dir = path.value.value().Dirname(); !dir.IsError()) {
        options.path = dir.value.value();
      }
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
