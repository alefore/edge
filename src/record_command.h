#ifndef __AFC_EDITOR_RECORD_COMMAND_H__
#define __AFC_EDITOR_RECORD_COMMAND_H__

#include <memory>

#include "command.h"

namespace afc {
namespace editor {

std::unique_ptr<Command> NewRecordCommand(EditorState& editor_state);

}  // namespace editor
}  // namespace afc

#endif  // __AFC_EDITOR_RECORD_COMMAND_H__
