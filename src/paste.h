#ifndef __AFC_EDITOR_PASTE_H__
#define __AFC_EDITOR_PASTE_H__

#include <memory>

#include "src/language/gc.h"

namespace afc::editor {
class EditorState;
class Command;
language::gc::Root<Command> NewPasteCommand(EditorState& editor_state);
}  // namespace afc::editor

#endif  // __AFC_EDITOR_PASTE_H__
