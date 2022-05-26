#ifndef __AFC_EDITOR_COMMAND_WITH_MODIFIERS_H__
#define __AFC_EDITOR_COMMAND_WITH_MODIFIERS_H__

#include <memory>

#include "src/buffer.h"
#include "src/command.h"
#include "src/editor.h"
#include "src/language/safe_types.h"
#include "src/terminal.h"

namespace afc::editor {

using CommandWithModifiersHandler =
    std::function<transformation::Variant(Modifiers)>;

language::NonNull<std::unique_ptr<Command>> NewCommandWithModifiers(
    std::function<std::wstring(const Modifiers&)> name_function,
    std::wstring description, Modifiers initial_modifiers,
    CommandWithModifiersHandler handler, EditorState& editor_state);

}  // namespace afc::editor

#endif  // __AFC_EDITOR_COMMAND_WITH_MODIFIERS_H__
