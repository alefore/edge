#ifndef __AFC_EDITOR_RUN_COMMAND_HANDLER_H__
#define __AFC_EDITOR_RUN_COMMAND_HANDLER_H__

#include <string>

namespace afc {
namespace editor {

using std::string;

struct EditorState;

void RunCommandHandler(const string& input, EditorState* editor_state);
void RunMultipleCommandsHandler(const string& input, EditorState* editor_state);

}  // namespace editor
}  // namespace afc

#endif
