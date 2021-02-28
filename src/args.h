#ifndef __AFC_EDITOR_SRC_ARGS_H__
#define __AFC_EDITOR_SRC_ARGS_H__

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "src/command_line.h"
#include "src/dirname.h"

namespace afc::editor {

using std::wstring;

struct CommandLineValues : public command_line_arguments::StandardArguments {
  CommandLineValues();

  Path home_directory;

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

  // If non-empty, benchmark to run.
  std::wstring benchmark = L"";

  enum class TestsBehavior { kIgnore, kRunAndExit, kListAndExit };
  TestsBehavior tests_behavior = TestsBehavior::kIgnore;

  enum class ViewMode {
    // Automatically start editing all files opened (as soon as they have been
    // loaded).
    kAllBuffers,
    // Default mode (where only a given file is edited).
    kDefault
  };
  ViewMode view_mode = ViewMode::kDefault;

  double frames_per_second = 30.0;
};

const std::vector<afc::command_line_arguments::Handler<CommandLineValues>>&
CommandLineArgs();

}  // namespace afc::editor

#endif  // __AFC_EDITOR_SRC_ARGS_H__
