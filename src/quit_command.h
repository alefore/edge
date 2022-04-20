#ifndef __AFC_EDITOR_QUIT_COMMAND_H__
#define __AFC_EDITOR_QUIT_COMMAND_H__

#include <memory>

namespace afc::editor {
class Command;
class EditorState;
std::unique_ptr<Command> NewQuitCommand(EditorState& editor_state,
                                        int exit_value);
}  // namespace afc::editor

#endif  // __AFC_EDITOR_QUIT_COMMAND_H__
