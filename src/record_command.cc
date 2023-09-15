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
#include "src/language/lazy_string/char_buffer.h"

namespace afc::editor {
using language::MakeNonNullShared;
using language::MakeNonNullUnique;
using language::NonNull;
using language::VisitPointer;
using language::lazy_string::NewLazyString;
using language::text::Line;

namespace gc = language::gc;

class RecordCommand : public Command {
 public:
  RecordCommand(EditorState& editor_state) : editor_state_(editor_state) {}

  std::wstring Description() const override {
    return L"starts/stops recording a transformation";
  }
  std::wstring Category() const override { return L"Edit"; }

  void ProcessInput(wint_t) override {
    VisitPointer(
        editor_state_.current_buffer(),
        [](gc::Root<OpenBuffer> buffer_root) {
          OpenBuffer& buffer = buffer_root.ptr().value();
          if (buffer.HasTransformationStack()) {
            buffer.PopTransformationStack();
            buffer.status().SetInformationText(
                MakeNonNullShared<Line>(L"Recording: stop"));
          } else {
            buffer.PushTransformationStack();
            buffer.status().SetInformationText(
                MakeNonNullShared<Line>(L"Recording: start"));
          }
          buffer.ResetMode();
        },
        [] {});
  }

 private:
  EditorState& editor_state_;
};

NonNull<std::unique_ptr<Command>> NewRecordCommand(EditorState& editor_state) {
  return MakeNonNullUnique<RecordCommand>(editor_state);
}

}  // namespace afc::editor
