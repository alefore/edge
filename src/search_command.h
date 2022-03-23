#ifndef __AFC_EDITOR_SEARCH_COMMAND_H__
#define __AFC_EDITOR_SEARCH_COMMAND_H__

#include <memory>

#include "command.h"

namespace afc {
namespace editor {

std::unique_ptr<Command> NewSearchCommand(EditorState& editor_state);

}  // namespace editor
}  // namespace afc

#endif  // __AFC_EDITOR_SEARCH_COMMAND_H__
