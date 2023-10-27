#ifndef __AFC_EDITOR_QUIT_COMMAND_H__
#define __AFC_EDITOR_QUIT_COMMAND_H__

#include <memory>

#include "src/language/gc.h"

namespace afc::editor {
class Command;
class EditorState;
language::gc::Root<Command> NewQuitCommand(EditorState& editor_state,
                                           int exit_value);
}  // namespace afc::editor

#endif  // __AFC_EDITOR_QUIT_COMMAND_H__
