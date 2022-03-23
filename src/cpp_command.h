#ifndef __AFC_EDITOR_CPP_COMMAND_H__
#define __AFC_EDITOR_CPP_COMMAND_H__

#include <memory>
#include <string>

#include "command.h"
#include "vm/public/vm.h"

namespace afc {
namespace editor {

std::unique_ptr<Command> NewCppCommand(
    EditorState& editor_state,
    std::shared_ptr<afc::vm::Environment> environment, const std::wstring code);

}  // namespace editor
}  // namespace afc

#endif  // __AFC_EDITOR_CPP_COMMAND_H__
