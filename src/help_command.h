#ifndef __AFC_EDITOR_HELP_COMMAND_H__
#define __AFC_EDITOR_HELP_COMMAND_H__

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "command.h"
#include "map_mode.h"

namespace afc {
namespace editor {

using std::map;
using std::unique_ptr;
using std::vector;
using std::wstring;

std::unique_ptr<Command> NewHelpCommand(const MapModeCommands* commands,
                                        const std::wstring& mode_description);

}  // namespace editor
}  // namespace afc

#endif
