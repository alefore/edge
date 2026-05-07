#ifndef __AFC_EDITOR_SRC_CHILD_PROCESS_TRACKER_H__
#define __AFC_EDITOR_SRC_CHILD_PROCESS_TRACKER_H__

#include <optional>

#include "src/buffer_flag_map.h"
#include "src/futures/futures.h"
#include "src/infrastructure/file_system_driver.h"
#include "src/language/error/value_or_error.h"
#include "src/language/safe_types.h"

namespace afc::editor {
class ChildProcessTracker {
  std::optional<infrastructure::ProcessId> pid_ = std::nullopt;
  std::optional<int> exit_status_;
  struct timespec time_last_exit_;
  // Optional function to execute when a sub-process exits.
  std::optional<language::OnceOnlyFunction<void()>> on_exit_handler_;

 public:
  std::optional<infrastructure::ProcessId> pid() const;
  void set_pid(std::optional<infrastructure::ProcessId> pid);
  futures::Value<language::EmptyValue> WaitPid(
      language::NonNull<std::shared_ptr<infrastructure::FileSystemDriver>>
          file_system_driver);

  futures::PossibleError KillAndWaitForTermination(
      infrastructure::FileSystemDriver&);

  std::optional<int> exit_status() const;
  // Also resets pid_.
  void set_exit_status(int status);
  // It is an error (and will crash) to call this when exit_status isn't set.
  struct timespec time_last_exit() const;
  void set_on_exit_handler(language::OnceOnlyFunction<void()>);
  bool is_dirty() const;

  void GetFlags(BufferFlagMap& output) const;
};
}  // namespace afc::editor
#endif
