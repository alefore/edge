#ifndef __AFC_EDITOR_SRC_ARGS_H__
#define __AFC_EDITOR_SRC_ARGS_H__

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "src/command_line.h"

namespace afc::editor {

using std::wstring;

struct CommandLineValues : public command_line_arguments::StandardArguments {
  CommandLineValues();

  std::wstring home_directory;

  std::vector<std::wstring> commands_to_fork;

  // Contains C++ (VM) code to execute.
  wstring commands_to_run;

  bool server = false;
  wstring server_path = L"";

  // If non-empty, path of the server to connect to.
  wstring client = L"";

  bool mute = false;
  bool background = false;

  enum class NestedEdgeBehavior {
    // Wait until the buffers we open have been closed in the parent.
    kWaitForClose,
    // Exit as soon as we know that we've successfully communicated with the
    // parent.
    kExitEarly,
  };

  NestedEdgeBehavior nested_edge_behavior = NestedEdgeBehavior::kWaitForClose;

  // If true, after creating all buffers specified by other command line flags,
  // start a prompt for opening a file.
  bool prompt_for_path = false;
};

const std::vector<afc::command_line_arguments::Handler<CommandLineValues>>&
CommandLineArgs();

}  // namespace afc::editor

#endif  // __AFC_EDITOR_SRC_ARGS_H__
