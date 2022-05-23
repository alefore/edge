#ifndef __AFC_EDITOR_PASTE_H__
#define __AFC_EDITOR_PASTE_H__

#include <memory>

#include "src/language/safe_types.h"

namespace afc::editor {
class EditorState;
class Command;
language::NonNull<std::unique_ptr<Command>> NewPasteCommand(
    EditorState& editor_state);
}  // namespace afc::editor

#endif  // __AFC_EDITOR_PASTE_H__
