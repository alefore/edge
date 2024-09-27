#include "src/open_directory_command.h"

extern "C" {
#include <libgen.h>
}

#include "src/buffer_variables.h"
#include "src/command.h"
#include "src/editor.h"
#include "src/file_link_mode.h"
#include "src/infrastructure/dirname.h"
#include "src/language/lazy_string/lazy_string.h"
#include "src/language/wstring.h"

namespace gc = afc::language::gc;

using afc::infrastructure::ExtendedChar;
using afc::infrastructure::Path;
using afc::language::MakeNonNullUnique;
using afc::language::NonNull;
using afc::language::ValueOrError;
using afc::language::VisitPointer;
using afc::language::lazy_string::LazyString;

namespace afc::editor {
namespace {

class OpenDirectoryCommand : public Command {
  EditorState& editor_state_;

 public:
  OpenDirectoryCommand(EditorState& editor_state)
      : editor_state_(editor_state) {}

  LazyString Description() const override {
    return LazyString{L"opens a view of the current directory"};
  }
  CommandCategory Category() const override {
    return CommandCategory::kBuffers();
  }

  void ProcessInput(ExtendedChar) override {
    OpenOrCreateFile(OpenFileOptions{
        .editor_state = editor_state_,
        .path = OptionalFrom(VisitPointer(
            editor_state_.current_buffer(),
            [](gc::Root<OpenBuffer> buffer) -> ValueOrError<Path> {
              ASSIGN_OR_RETURN(
                  Path path,
                  Path::New(buffer.ptr()->Read(buffer_variables::name)));
              return path.Dirname();
            },
            [] { return Path::LocalDirectory(); }))});
  }

  std::vector<language::NonNull<std::shared_ptr<language::gc::ObjectMetadata>>>
  Expand() const override {
    return {};
  }
};

}  // namespace

gc::Root<Command> NewOpenDirectoryCommand(EditorState& editor_state) {
  return editor_state.gc_pool().NewRoot(
      MakeNonNullUnique<OpenDirectoryCommand>(editor_state));
}

}  // namespace afc::editor
