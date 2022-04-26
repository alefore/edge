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

namespace afc::editor {
using language::MakeNonNullUnique;
using language::NonNull;

class RecordCommand : public Command {
 public:
  RecordCommand(EditorState& editor_state) : editor_state_(editor_state) {}

  wstring Description() const override {
    return L"starts/stops recording a transformation";
  }
  wstring Category() const override { return L"Edit"; }

  void ProcessInput(wint_t) override {
    auto buffer = editor_state_.current_buffer();
    if (buffer == nullptr) {
      return;
    }
    if (buffer->HasTransformationStack()) {
      buffer->PopTransformationStack();
      buffer->status().SetInformationText(L"Recording: stop");
    } else {
      buffer->PushTransformationStack();
      buffer->status().SetInformationText(L"Recording: start");
    }
    buffer->ResetMode();
  }

 private:
  EditorState& editor_state_;
};

NonNull<std::unique_ptr<Command>> NewRecordCommand(EditorState& editor_state) {
  return MakeNonNullUnique<RecordCommand>(editor_state);
}

}  // namespace afc::editor
