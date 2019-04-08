#include "record_command.h"

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

#include "buffer.h"
#include "command.h"
#include "editor.h"

namespace afc {
namespace editor {

class RecordCommand : public Command {
  wstring Description() const override {
    return L"starts/stops recording a transformation";
  }
  wstring Category() const override { return L"Edit"; }

  void ProcessInput(wint_t, EditorState* editor_state) override {
    if (!editor_state->has_current_buffer()) {
      return;
    }
    auto buffer = editor_state->current_buffer()->second;
    if (buffer->HasTransformationStack()) {
      buffer->PopTransformationStack();
      editor_state->SetStatus(L"Recording: stop");
    } else {
      buffer->PushTransformationStack();
      editor_state->SetStatus(L"Recording: start");
    }
    buffer->ResetMode();
  }
};

std::unique_ptr<Command> NewRecordCommand() {
  return std::make_unique<RecordCommand>();
}

}  // namespace editor
}  // namespace afc
