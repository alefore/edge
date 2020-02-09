#ifndef __AFC_EDITOR_COMMAND_MODE_H__
#define __AFC_EDITOR_COMMAND_MODE_H__

#include <functional>
#include <list>
#include <memory>
#include <string>
#include <vector>

#include "src/map_mode.h"

namespace afc::editor {
class EditorState;
std::unique_ptr<MapModeCommands> NewCommandMode(EditorState* editor_state);
}  // namespace afc::editor

#endif
