#include <optional>

#include "src/infrastructure/file_system_driver.h"

namespace afc::editor {
class ChildProcessTracker {
  std::optional<infrastructure::ProcessId> pid_ = std::nullopt;

 public:
  std::optional<infrastructure::ProcessId> pid() const;
  void set_pid(std::optional<infrastructure::ProcessId> pid);

  bool is_dirty() const;
};
}  // namespace afc::editor
