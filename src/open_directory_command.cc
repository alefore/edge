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
using language::ValueOrError;
using language::VisitPointer;
namespace {
using infrastructure::Path;

namespace gc = language::gc;

class OpenDirectoryCommand : public Command {
 public:
  OpenDirectoryCommand(EditorState& editor_state)
      : editor_state_(editor_state) {}

  std::wstring Description() const override {
    return L"opens a view of the current directory";
  }
  std::wstring Category() const override { return L"Buffers"; }

  void ProcessInput(wint_t) override {
    OpenOrCreateFile(OpenFileOptions{
        .editor_state = editor_state_,
        .path = VisitPointer(
                    editor_state_.current_buffer(),
                    [](gc::Root<OpenBuffer> buffer) -> ValueOrError<Path> {
                      ASSIGN_OR_RETURN(Path path,
                                       Path::FromString(buffer.ptr()->Read(
                                           buffer_variables::name)));
                      return path.Dirname();
                    },
                    [] { return Path::LocalDirectory(); })
                    .AsOptional()});
  }

 private:
  EditorState& editor_state_;
};

}  // namespace

NonNull<std::unique_ptr<Command>> NewOpenDirectoryCommand(
    EditorState& editor_state) {
  return MakeNonNullUnique<OpenDirectoryCommand>(editor_state);
}

}  // namespace afc::editor
