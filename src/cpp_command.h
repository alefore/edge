#ifndef __AFC_EDITOR_CPP_COMMAND_H__
#define __AFC_EDITOR_CPP_COMMAND_H__

#include <memory>
#include <string>

#include "src/language/error/value_or_error.h"
#include "src/language/gc.h"
#include "src/vm/vm.h"

namespace afc::editor {
class EditorState;
class Command;

language::ValueOrError<language::gc::Root<Command>> NewCppCommand(
    EditorState& editor_state,
    language::gc::Root<afc::vm::Environment> environment,
    const std::wstring code);
}  // namespace afc::editor

#endif  // __AFC_EDITOR_CPP_COMMAND_H__
