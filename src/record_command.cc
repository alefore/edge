#include "record_command.h"

#include <cerrno>
#include <cstring>
#include <unistd.h>
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
  const wstring Description() {
    return L"starts/stops recording a transformation";
  }

  void ProcessInput(wint_t, EditorState* editor_state) {
    if (!editor_state->has_current_buffer()) { return; }
    auto buffer = editor_state->current_buffer();
    if (buffer->second->HasTransformationStack()) {
      buffer->second->PopTransformationStack();
      editor_state->SetStatus(L"Recording: stop");
    } else {
      buffer->second->PushTransformationStack();
      editor_state->SetStatus(L"Recording: start");
    }
    editor_state->ResetMode();
  }
};

std::unique_ptr<Command> NewRecordCommand() {
  return std::unique_ptr<Command>(new RecordCommand());
}

}  // namespace afc
}  // namespace editor
