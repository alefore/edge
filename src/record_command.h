#ifndef __AFC_EDITOR_RECORD_COMMAND_H__
#define __AFC_EDITOR_RECORD_COMMAND_H__

#include <memory>

#include "src/language/safe_types.h"
namespace afc::editor {
class Command;
class EditorState;
language::NonNull<std::unique_ptr<Command>> NewRecordCommand(
    EditorState& editor_state);
}  // namespace afc::editor

#endif  // __AFC_EDITOR_RECORD_COMMAND_H__
