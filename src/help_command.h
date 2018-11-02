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

// The reason this takes a vector of maps (rather than just a single map) is to
// allow hierachies of delegating MapMode (the main customer of this class),
// where each instance may modify its bindings (after this instance has been
// built).
unique_ptr<Command> NewHelpCommand(
    std::vector<const map<vector<wint_t>, Command*>*> commands,
    const std::wstring& mode_description);

}  // namespace editor
}  // namespace afc

#endif
