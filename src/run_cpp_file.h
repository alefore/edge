#ifndef __AFC_EDITOR_RUN_CPP_FILE_H__
#define __AFC_EDITOR_RUN_CPP_FILE_H__

#include <memory>

#include "src/language/gc.h"
#include "src/language/lazy_string/lazy_string.h"

namespace afc::editor {
class EditorState;
class Command;
language::gc::Root<Command> NewRunCppFileCommand(EditorState& editor_state);
}  // namespace afc::editor

#endif  // __AFC_EDITOR_RUN_CPP_FILE_H__
