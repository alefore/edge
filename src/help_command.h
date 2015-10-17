#ifndef __AFC_EDITOR_HELP_COMMAND_H__
#define __AFC_EDITOR_HELP_COMMAND_H__

#include <memory>
#include <map>
#include <string>
#include <vector>

#include "command.h"

namespace afc {
namespace editor {

using std::map;
using std::unique_ptr;
using std::vector;
using std::wstring;

unique_ptr<Command> NewHelpCommand(
    const map<vector<wint_t>, Command*>& commands,
    const std::wstring& mode_description);

}  // namespace editor
}  // namespace afc

#endif
