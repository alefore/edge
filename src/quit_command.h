#ifndef __AFC_EDITOR_QUIT_COMMAND_H__
#define __AFC_EDITOR_QUIT_COMMAND_H__

#include <memory>

#include "command.h"

namespace afc {
namespace editor {
std::unique_ptr<Command> NewQuitCommand(EditorState& editor_state,
                                        int exit_value);
}  // namespace editor
}  // namespace afc

#endif  // __AFC_EDITOR_QUIT_COMMAND_H__
