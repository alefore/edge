#ifndef __AFC_EDITOR_RUN_CPP_FILE_H__
#define __AFC_EDITOR_RUN_CPP_FILE_H__

#include <memory>

#include "src/language/lazy_string/lazy_string.h"
#include "src/language/safe_types.h"

namespace afc::editor {
class EditorState;
class Command;
language::NonNull<std::unique_ptr<Command>> NewRunCppFileCommand(
    EditorState& editor_state);
}  // namespace afc::editor

#endif  // __AFC_EDITOR_RUN_CPP_FILE_H__
