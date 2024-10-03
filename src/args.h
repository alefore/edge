#ifndef __AFC_EDITOR_SRC_ARGS_H__
#define __AFC_EDITOR_SRC_ARGS_H__

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "src/infrastructure/command_line.h"
#include "src/infrastructure/dirname.h"
#include "src/tests/benchmarks.h"

namespace afc::editor {

struct CommandLineValues : public command_line_arguments::StandardArguments {
  CommandLineValues();

  infrastructure::Path home_directory;

  std::vector<language::lazy_string::LazyString> commands_to_fork;

  // Contains C++ (VM) code to execute.
  language::lazy_string::LazyString commands_to_run;

  bool server = false;
  std::optional<infrastructure::Path> server_path = {};

  // If non-empty, path of the server to connect to.
  std::optional<infrastructure::Path> client = {};

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

  // If present, benchmark to run.
  std::optional<tests::BenchmarkName> benchmark;

  enum class ViewMode {
    // Automatically start editing all files opened (as soon as they have been
    // loaded).
    kAllBuffers,
    // Default mode (where only a given file is edited).
    kDefault
  };
  ViewMode view_mode = ViewMode::kDefault;

  double frames_per_second = 30.0;

  enum class LocalPathResolutionBehavior {
    // A local path is interpreted as relative to the current directory of the
    // Edge client instance.
    kSimple,
    // A local path is given to the Edge server, allowing it to do a full
    // resolution (e.g., including looking it up in the configured search
    // paths).
    kAdvanced
  };
  LocalPathResolutionBehavior initial_path_resolution_behavior =
      LocalPathResolutionBehavior::kSimple;

  enum class HistoryFileBehavior { kUpdate, kReadOnly };
  HistoryFileBehavior prompt_history_behavior = HistoryFileBehavior::kUpdate;

  HistoryFileBehavior positions_history_behavior = HistoryFileBehavior::kUpdate;
};

const std::vector<afc::command_line_arguments::Handler<CommandLineValues>>&
CommandLineArgs();

language::lazy_string::LazyString CommandsToRun(CommandLineValues args);

}  // namespace afc::editor

#endif  // __AFC_EDITOR_SRC_ARGS_H__
