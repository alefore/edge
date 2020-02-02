#ifndef __AFC_EDITOR_NAVIGATE_COMMAND_H__
#define __AFC_EDITOR_NAVIGATE_COMMAND_H__

#include <memory>

#include "command.h"

namespace afc::editor {
class EditorState;
std::unique_ptr<Command> NewNavigateCommand(EditorState* editor_state);
}  // namespace afc::editor

#endif  // __AFC_EDITOR_NAVIGATE_COMMAND_H__
