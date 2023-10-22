#ifndef __AFC_EDITOR_COMMAND_MODE_H__
#define __AFC_EDITOR_COMMAND_MODE_H__

#include <memory>

#include "src/language/gc.h"
#include "src/map_mode.h"

namespace afc::editor {
class EditorState;
language::NonNull<std::unique_ptr<MapModeCommands>> NewCommandMode(
    EditorState& editor_state);
}  // namespace afc::editor

#endif
