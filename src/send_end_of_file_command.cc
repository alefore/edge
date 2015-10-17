#include "send_end_of_file_command.h"

extern "C" {
#include <sys/socket.h>
}

#include "command.h"
#include "editor.h"
#include "file_link_mode.h"
#include "line_prompt_mode.h"
#include "wstring.h"

namespace afc {
namespace editor {

void SendEndOfFileToBuffer(EditorState* editor_state,
                           std::shared_ptr<OpenBuffer> buffer) {
  if (editor_state->structure() == LINE) {
    editor_state->ResetStructure();
    DLOG(INFO) << "Sending EOF to line: "
               << buffer->current_line()->ToString();
    if (buffer->current_line()->activate() != nullptr) {
      buffer->current_line()->activate()->ProcessInput(0, editor_state);
    }
    return;
  }
  if (buffer->fd() == -1) {
    editor_state->SetStatus(L"No active subprocess for current buffer.");
    return;
  }
  if (buffer->read_bool_variable(OpenBuffer::variable_pts())) {
    char str[1] = { 4 };
    if (write(buffer->fd(), str, sizeof(str)) == -1) {
      editor_state->SetStatus(
          L"Sending EOF failed: " + FromByteString(strerror(errno)));
      return;
    }
    editor_state->SetStatus(L"EOF sent");
  } else {
    if (shutdown(buffer->fd(), SHUT_WR) == -1) {
      editor_state->SetStatus(
          L"shutdown(SHUT_WR) failed: " + FromByteString(strerror(errno)));
      return;
    }
    editor_state->SetStatus(L"shutdown sent");
  }
}

class SendEndOfFileCommand : public Command {
 public:
  const wstring Description() {
    return L"stops writing to a subprocess (effectively sending EOF).";
  }

  void ProcessInput(wint_t, EditorState* editor_state) {
    editor_state->ResetMode();
    if (!editor_state->has_current_buffer()) { return; }
    SendEndOfFileToBuffer(editor_state, editor_state->current_buffer()->second);
  }
};

std::unique_ptr<Command> NewSendEndOfFileCommand() {
  return std::unique_ptr<Command>(new SendEndOfFileCommand());
}

}  // namespace afc
}  // namespace editor
