#ifndef __AFC_EDITOR_OPEN_DIRECTORY_COMMAND_H__
#define __AFC_EDITOR_OPEN_DIRECTORY_COMMAND_H__

#include <memory>

#include "src/language/gc.h"

namespace afc::editor {
class Command;
class EditorState;
language::gc::Root<Command> NewOpenDirectoryCommand(EditorState& editor_state);
}  // namespace afc::editor

#endif  // __AFC_EDITOR_OPEN_DIRECTORY_COMMAND_H__
