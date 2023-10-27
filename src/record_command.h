#ifndef __AFC_EDITOR_RECORD_COMMAND_H__
#define __AFC_EDITOR_RECORD_COMMAND_H__

#include <memory>

#include "src/language/gc.h"
namespace afc::editor {
class Command;
class EditorState;
language::gc::Root<Command> NewRecordCommand(EditorState& editor_state);
}  // namespace afc::editor

#endif  // __AFC_EDITOR_RECORD_COMMAND_H__
