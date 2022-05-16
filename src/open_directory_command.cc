#include "src/open_directory_command.h"

extern "C" {
#include <libgen.h>
}

#include "src/buffer_variables.h"
#include "src/command.h"
#include "src/editor.h"
#include "src/file_link_mode.h"
#include "src/infrastructure/dirname.h"
#include "src/language/wstring.h"

namespace afc::editor {
using language::MakeNonNullUnique;
using language::NonNull;
namespace {
using infrastructure::Path;

namespace gc = language::gc;

class OpenDirectoryCommand : public Command {
 public:
  OpenDirectoryCommand(EditorState& editor_state)
      : editor_state_(editor_state) {}

  wstring Description() const override {
    return L"opens a view of the current directory";
  }
  wstring Category() const override { return L"Buffers"; }

  void ProcessInput(wint_t) override {
    OpenOrCreateFile({.editor_state = editor_state_,
                      .path = GetPath(editor_state_.current_buffer())});
  }

 private:
  static std::optional<Path> GetPath(
      std::optional<gc::Root<OpenBuffer>> buffer) {
    // TODO(easy, 2022-05-16): Switch to VisitPointer.
    if (!buffer.has_value()) return Path::LocalDirectory();
    auto path = Path::FromString(buffer->ptr()->Read(buffer_variables::name));
    if (path.IsError()) return std::nullopt;
    auto dir = path.value().Dirname();
    if (dir.IsError()) return std::nullopt;
    return dir.value();
  }

  EditorState& editor_state_;
};

}  // namespace

NonNull<std::unique_ptr<Command>> NewOpenDirectoryCommand(
    EditorState& editor_state) {
  return MakeNonNullUnique<OpenDirectoryCommand>(editor_state);
}

}  // namespace afc::editor
