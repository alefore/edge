#ifndef __AFC_EDITOR_INFRASTRUCTURE_EXECUTION__
#define __AFC_EDITOR_INFRASTRUCTURE_EXECUTION__

#include <functional>
#include <optional>

#include "poll.h"
#include "src/infrastructure/file_system_driver.h"
#include "src/infrastructure/time.h"

namespace afc::infrastructure::execution {
class IterationHandler {
 public:
  virtual ~IterationHandler() = default;

  virtual void AddHandler(FileDescriptor, int requested_events,
                          std::function<void(int)> handler) = 0;
};

struct ExecutionEnvironmentOptions {
  std::function<bool()> stop_check;
  std::function<std::optional<Time>()> get_next_alarm;
  std::function<void()> on_signals;
  std::function<void(IterationHandler&)> on_iteration;
};

class ExecutionEnvironment {
  const ExecutionEnvironmentOptions options_;

 public:
  ExecutionEnvironment(ExecutionEnvironmentOptions options);

  void Run();
};
}  // namespace afc::infrastructure::execution

#endif