#ifndef __AFC_EDITOR_NAVIGATION_BUFFER_H__
#define __AFC_EDITOR_NAVIGATION_BUFFER_H__

#include <memory>

#include "src/language/gc.h"

namespace afc::editor {
class Command;
class EditorState;
language::gc::Root<Command> NewNavigationBufferCommand(
    EditorState& editor_state);
}  // namespace afc::editor

#endif  // __AFC_EDITOR_NAVIGATION_BUFFER_H__
