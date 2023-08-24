#ifndef __AFC_EDITOR_SET_VARIABLE_COMMAND_H__
#define __AFC_EDITOR_SET_VARIABLE_COMMAND_H__

#include <memory>
#include <string>

#include "src/futures/futures.h"
#include "src/language/lazy_string/lazy_string.h"
#include "src/language/safe_types.h"
#include "src/language/error/value_or_error.h"

namespace afc::editor {
class Command;
class EditorState;

// Shows a prompt that reads a value for a given variable.
futures::Value<language::EmptyValue> SetVariableCommandHandler(
    EditorState& editor_state,
    language::NonNull<std::shared_ptr<language::lazy_string::LazyString>>
        input_name);

language::NonNull<std::unique_ptr<Command>> NewSetVariableCommand(
    EditorState& editor_state);
}  // namespace afc::editor

#endif  // __AFC_EDITOR_SET_VARIABLE_COMMAND_H__
