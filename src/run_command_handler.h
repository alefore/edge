#ifndef __AFC_EDITOR_RUN_COMMAND_HANDLER_H__
#define __AFC_EDITOR_RUN_COMMAND_HANDLER_H__

#include <map>
#include <memory>
#include <string>

#include "command.h"

namespace afc {
namespace editor {

using std::map;
using std::string;
using std::unique_ptr;

struct EditorState;

struct ForkCommandOptions {
  ForkCommandOptions() : enter(false) {}

  // The command to run.
  string command;

  string buffer_name;

  // Additional environment variables (e.g. getenv) to give to the command.
  map<string, string> environment;

  // Should we make it the active buffer?
  bool enter;
};

unique_ptr<Command> NewForkCommand();

void ForkCommand(EditorState* editor_state, const ForkCommandOptions& options);
void RunCommandHandler(const string& input, EditorState* editor_state);
void RunMultipleCommandsHandler(const string& input, EditorState* editor_state);

}  // namespace editor
}  // namespace afc

#endif
