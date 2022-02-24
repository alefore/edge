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
    OpenFile({.editor_state = *editor_state,
              .path = GetPath(editor_state->current_buffer().get())});
  }

 private:
  static std::optional<Path> GetPath(const OpenBuffer* buffer) {
    if (buffer == nullptr) return Path::LocalDirectory();
    auto path = Path::FromString(buffer->Read(buffer_variables::name));
    if (path.IsError()) return std::nullopt;
    auto dir = path.value().Dirname();
    if (dir.IsError()) return std::nullopt;
    return dir.value();
  }
};

}  // namespace

std::unique_ptr<Command> NewOpenDirectoryCommand() {
  return std::make_unique<OpenDirectoryCommand>();
}

}  // namespace editor
}  // namespace afc
