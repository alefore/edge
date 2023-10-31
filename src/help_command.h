#ifndef __AFC_EDITOR_HELP_COMMAND_H__
#define __AFC_EDITOR_HELP_COMMAND_H__

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "command.h"
#include "map_mode.h"
#include "src/language/safe_types.h"
#include "src/language/text/line.h"

namespace afc::editor {
language::text::Line DescribeSequence(const std::wstring& input);
language::text::Line DescribeSequenceWithQuotes(const std::wstring& input);

language::gc::Root<Command> NewHelpCommand(
    EditorState& editor_state, const MapModeCommands& commands,
    const std::wstring& mode_description);

}  // namespace afc::editor

#endif
