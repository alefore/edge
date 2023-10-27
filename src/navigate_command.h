#ifndef __AFC_EDITOR_NAVIGATE_COMMAND_H__
#define __AFC_EDITOR_NAVIGATE_COMMAND_H__

#include <memory>

#include "src/language/gc.h"

namespace afc::editor {
class EditorState;
class Command;
language::gc::Root<Command> NewNavigateCommand(EditorState& editor_state);
}  // namespace afc::editor

#endif  // __AFC_EDITOR_NAVIGATE_COMMAND_H__
