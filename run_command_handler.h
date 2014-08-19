#ifndef __AFC_EDITOR_RUN_COMMAND_HANDLER_H__
#define __AFC_EDITOR_RUN_COMMAND_HANDLER_H__

#include <memory>
#include <string>

#include "command.h"

namespace afc {
namespace editor {

using std::string;
using std::unique_ptr;

struct EditorState;

unique_ptr<Command> NewForkCommand();

void RunCommandHandler(const string& input, EditorState* editor_state);
void RunMultipleCommandsHandler(const string& input, EditorState* editor_state);

}  // namespace editor
}  // namespace afc

#endif
