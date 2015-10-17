#ifndef __AFC_EDITOR_SEND_END_OF_FILE_COMMAND_H__
#define __AFC_EDITOR_SEND_END_OF_FILE_COMMAND_H__

#include <memory>

#include "buffer.h"
#include "command.h"
#include "editor.h"

namespace afc {
namespace editor {

void SendEndOfFileToBuffer(EditorState* editor_state,
                           std::shared_ptr<OpenBuffer> buffer);

std::unique_ptr<Command> NewSendEndOfFileCommand();

}  // namespace editor
}  // namespace afc

#endif  // __AFC_EDITOR_SEND_END_OF_FILE_COMMAND_H__
