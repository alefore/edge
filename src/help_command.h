#ifndef __AFC_EDITOR_HELP_COMMAND_H__
#define __AFC_EDITOR_HELP_COMMAND_H__

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "command.h"
#include "map_mode.h"
#include "src/language/safe_types.h"

namespace afc::editor {

language::NonNull<std::unique_ptr<Command>> NewHelpCommand(
    EditorState& editor_state, const MapModeCommands* commands,
    const std::wstring& mode_description);

}  // namespace afc::editor

#endif
