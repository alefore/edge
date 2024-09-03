#include "src/record_command.h"

#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <iostream>
#include <map>
#include <memory>

extern "C" {
#include <libgen.h>
#include <sys/socket.h>
}

#include "src/buffer.h"
#include "src/command.h"
#include "src/editor.h"
#include "src/infrastructure/extended_char.h"
#include "src/language/lazy_string/char_buffer.h"

namespace gc = afc::language::gc;

using afc::infrastructure::ExtendedChar;
using afc::language::MakeNonNullShared;
using afc::language::MakeNonNullUnique;
using afc::language::NonNull;
using afc::language::VisitPointer;
using afc::language::lazy_string::LazyString;
using afc::language::text::Line;

namespace afc::editor {
class RecordCommand : public Command {
 public:
  RecordCommand(EditorState& editor_state) : editor_state_(editor_state) {}

  LazyString Description() const override {
    return LazyString{L"starts/stops recording a transformation"};
  }

  LazyString Category() const override { return LazyString{L"Edit"}; }

  void ProcessInput(ExtendedChar) override {
    VisitPointer(
        editor_state_.current_buffer(),
        [](gc::Root<OpenBuffer> buffer_root) {
          OpenBuffer& buffer = buffer_root.ptr().value();
          if (buffer.HasTransformationStack()) {
            buffer.PopTransformationStack();
            buffer.status().SetInformationText(Line(L"Recording: stop"));
          } else {
            buffer.PushTransformationStack();
            buffer.status().SetInformationText(Line(L"Recording: start"));
          }
          buffer.ResetMode();
        },
        [] {});
  }

  std::vector<language::NonNull<std::shared_ptr<language::gc::ObjectMetadata>>>
  Expand() const override {
    return {};
  }

 private:
  EditorState& editor_state_;
};

gc::Root<Command> NewRecordCommand(EditorState& editor_state) {
  return editor_state.gc_pool().NewRoot(
      MakeNonNullUnique<RecordCommand>(editor_state));
}

}  // namespace afc::editor
