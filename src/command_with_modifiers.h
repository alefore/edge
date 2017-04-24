#ifndef __AFC_EDITOR_COMMAND_WITH_MODIFIERS_H__
#define __AFC_EDITOR_COMMAND_WITH_MODIFIERS_H__

#include <memory>

#include "buffer.h"
#include "command.h"
#include "editor.h"

namespace afc {
namespace editor {

enum CommandApplyMode {
  // Just preview what this transformation would do. Don't apply any
  // long-lasting effects.
  APPLY_PREVIEW,
  // Apply the transformation.
  APPLY_FINAL,
};

using CommandWithModifiersHandler =
    std::function<void(EditorState*, OpenBuffer*, CommandApplyMode, Modifiers)>;

std::unique_ptr<Command> NewCommandWithModifiers(
    wstring name, wstring description, CommandWithModifiersHandler handler);

}  // namespace editor
}  // namespace afc

#endif  // __AFC_EDITOR_COMMAND_WITH_MODIFIERS_H__
