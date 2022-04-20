#ifndef __AFC_EDITOR_LIST_BUFFERS_COMMAND_H__
#define __AFC_EDITOR_LIST_BUFFERS_COMMAND_H__

#include <memory>

namespace afc::editor {
class EditorState;
class Command;
std::unique_ptr<Command> NewListBuffersCommand(EditorState& editor_state);
}  // namespace afc::editor

#endif  // __AFC_EDITOR_LIST_BUFFERS_COMMAND_H__
