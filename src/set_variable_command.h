#ifndef __AFC_EDITOR_SET_VARIABLE_COMMAND_H__
#define __AFC_EDITOR_SET_VARIABLE_COMMAND_H__

#include <memory>
#include <string>

#include "src/command.h"
#include "src/futures/futures.h"

namespace afc::editor {
// Shows a prompt that reads a value for a given variable.
futures::Value<EmptyValue> SetVariableCommandHandler(
    const std::wstring& input_name, EditorState* editor_state);
std::unique_ptr<Command> NewSetVariableCommand(EditorState* editor_state);
}  // namespace afc::editor

#endif  // __AFC_EDITOR_SET_VARIABLE_COMMAND_H__
