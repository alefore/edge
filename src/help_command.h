#ifndef __AFC_EDITOR_HELP_COMMAND_H__
#define __AFC_EDITOR_HELP_COMMAND_H__

#include <memory>
#include <map>

#include "command.h"

namespace afc {
namespace editor {

using std::map;
using std::unique_ptr;

unique_ptr<Command> NewHelpCommand(const map<int, Command*>& commands,
                                   const string& mode_description);

}  // namespace editor
}  // namespace afc

#endif
