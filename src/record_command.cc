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

namespace afc {
namespace editor {

class RecordCommand : public Command {
  wstring Description() const override {
    return L"starts/stops recording a transformation";
  }
  wstring Category() const override { return L"Edit"; }

  void ProcessInput(wint_t, EditorState* editor_state) override {
    auto buffer = editor_state->current_buffer();
    if (buffer == nullptr) {
      return;
    }
    if (buffer->HasTransformationStack()) {
      buffer->PopTransformationStack();
      buffer->status()->SetInformationText(L"Recording: stop");
    } else {
      buffer->PushTransformationStack();
      buffer->status()->SetInformationText(L"Recording: start");
    }
    buffer->ResetMode();
  }
};

std::unique_ptr<Command> NewRecordCommand() {
  return std::make_unique<RecordCommand>();
}

}  // namespace editor
}  // namespace afc
