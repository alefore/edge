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
using std::wstring;

class EditorState;

struct ForkCommandOptions {
  ForkCommandOptions() : enter(false) {}

  // The command to run.
  wstring command;

  wstring buffer_name;

  // Additional environment variables (e.g. getenv) to give to the command.
  map<wstring, wstring> environment;

  // Should we make it the active buffer?
  bool enter;

  // If non-empty, change to this directory in the children. Ignored if empty.
  wstring children_path;
};

unique_ptr<Command> NewForkCommand();

class OpenBuffer;

std::shared_ptr<OpenBuffer> ForkCommand(EditorState* editor_state,
                                        const ForkCommandOptions& options);

void RunCommandHandler(const wstring& input, EditorState* editor_state,
                       std::map<wstring, wstring> environment);
void RunMultipleCommandsHandler(const wstring& input,
                                EditorState* editor_state);

}  // namespace editor
}  // namespace afc

#endif
