#ifndef __AFC_EDITOR_SET_VARIABLE_COMMAND_H__
#define __AFC_EDITOR_SET_VARIABLE_COMMAND_H__

#include <memory>
#include <string>

#include "src/futures/futures.h"
#include "src/language/error/value_or_error.h"
#include "src/language/gc.h"
#include "src/language/lazy_string/single_line.h"
#include "src/language/safe_types.h"

namespace afc::editor {
class Command;
class EditorState;

// Shows a prompt that reads a value for a given variable.
futures::Value<language::EmptyValue> SetVariableCommandHandler(
    EditorState& editor_state, language::lazy_string::SingleLine input_name);

language::gc::Root<Command> NewSetVariableCommand(EditorState& editor_state);
}  // namespace afc::editor

#endif  // __AFC_EDITOR_SET_VARIABLE_COMMAND_H__
