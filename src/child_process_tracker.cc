#include "src/child_process_tracker.h"

#include <glog/logging.h>

using afc::infrastructure::ProcessId;

namespace afc::editor {
std::optional<ProcessId> ChildProcessTracker::pid() const { return pid_; }

void ChildProcessTracker::set_pid(
    std::optional<infrastructure::ProcessId> pid) {
  CHECK(!pid_);
  pid_ = pid;
}

bool ChildProcessTracker::is_dirty() const { return pid_.has_value(); }

}  // namespace afc::editor
