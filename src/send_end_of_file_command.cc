#include "src/send_end_of_file_command.h"

extern "C" {
#include <sys/socket.h>
}

#include "src/buffer_variables.h"
#include "src/command.h"
#include "src/editor.h"
#include "src/file_link_mode.h"
#include "src/line_prompt_mode.h"
#include "src/wstring.h"

namespace afc {
namespace editor {

void SendEndOfFileToBuffer(EditorState* editor_state,
                           std::shared_ptr<OpenBuffer> buffer) {
  if (editor_state->structure() == StructureLine()) {
    auto target_buffer = buffer->GetBufferFromCurrentLine();
    if (target_buffer != nullptr) {
      LOG(INFO) << "Sending EOF to line: " << buffer->current_line()->ToString()
                << ": " << buffer->Read(buffer_variables::name());
      buffer = target_buffer;
    }
    editor_state->ResetModifiers();
  }

  if (buffer->fd() == -1) {
    editor_state->SetStatus(L"No active subprocess for current buffer.");
    return;
  }
  if (buffer->Read(buffer_variables::pts())) {
    char str[1] = {4};
    if (write(buffer->fd(), str, sizeof(str)) == -1) {
      editor_state->SetStatus(L"Sending EOF failed: " +
                              FromByteString(strerror(errno)));
      return;
    }
    editor_state->SetStatus(L"EOF sent");
  } else {
    if (shutdown(buffer->fd(), SHUT_WR) == -1) {
      editor_state->SetStatus(L"shutdown(SHUT_WR) failed: " +
                              FromByteString(strerror(errno)));
      return;
    }
    editor_state->SetStatus(L"shutdown sent");
  }
}

class SendEndOfFileCommand : public Command {
 public:
  wstring Description() const override {
    return L"stops writing to a subprocess (effectively sending EOF).";
  }
  wstring Category() const override { return L"Subprocess"; }

  void ProcessInput(wint_t, EditorState* editor_state) override {
    auto buffer = editor_state->current_buffer();
    if (buffer == nullptr) {
      return;
    }
    buffer->ResetMode();
    SendEndOfFileToBuffer(editor_state, buffer);
  }
};

std::unique_ptr<Command> NewSendEndOfFileCommand() {
  return std::make_unique<SendEndOfFileCommand>();
}

}  // namespace editor
}  // namespace afc
