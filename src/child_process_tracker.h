#ifndef __AFC_EDITOR_SRC_CHILD_PROCESS_TRACKER_H__
#define __AFC_EDITOR_SRC_CHILD_PROCESS_TRACKER_H__

#include <optional>

#include "src/buffer_flag_map.h"
#include "src/infrastructure/file_system_driver.h"

namespace afc::editor {
class ChildProcessTracker {
  std::optional<infrastructure::ProcessId> pid_ = std::nullopt;
  std::optional<int> exit_status_;

 public:
  std::optional<infrastructure::ProcessId> pid() const;
  void set_pid(std::optional<infrastructure::ProcessId> pid);

  std::optional<int> exit_status() const;
  // Also resets pid_.
  void set_exit_status(int status);

  bool is_dirty() const;

  void GetFlags(BufferFlagMap& output) const;
};
}  // namespace afc::editor
#endif
