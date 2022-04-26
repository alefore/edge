#ifndef __AFC_EDITOR_OPEN_FILE_COMMAND_H__
#define __AFC_EDITOR_OPEN_FILE_COMMAND_H__

#include <memory>

#include "src/language/safe_types.h"

namespace afc::editor {
class EditorState;
class Command;
language::NonNull<std::unique_ptr<Command>> NewOpenFileCommand(
    EditorState& editor);
}  // namespace afc::editor

#endif  // __AFC_EDITOR_OPEN_FILE_COMMAND_H__
