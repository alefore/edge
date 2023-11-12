#include "src/infrastructure/execution.h"

#include <poll.h>

#include <cmath>
#include <map>

#include "src/infrastructure/file_system_driver.h"
#include "src/language/container.h"

using afc::language::InsertOrDie;

namespace afc::infrastructure::execution {
namespace {
class IterationHandlerImpl : public IterationHandler {
  std::vector<std::pair<struct pollfd, std::function<void(int)>>> handler_;

 public:
  void AddHandler(FileDescriptor fd, int requested_events,
                  std::function<void(int)> handler) override {
    struct pollfd pollfd;
    pollfd.fd = fd.read();
    pollfd.events = requested_events;
    handler_.push_back({std::move(pollfd), std::move(handler)});
  }

  void Run(const ExecutionEnvironmentOptions& options) {
    // The file descriptor at position i will be either fd or fd_error
    // of buffers[i]. The exception to this is fd 0 (at the end).
    struct pollfd fds[handler_.size()];
    for (size_t i = 0; i < handler_.size(); i++) fds[i] = handler_[i].first;
    // buffers.reserve(sizeof(fds) / sizeof(fds[0]));
    auto now = Now();
    auto next_execution = options.get_next_alarm();
    int timeout_ms = next_execution.has_value()
                         ? static_cast<int>(ceil(std::min(
                               std::max(0.0, MillisecondsBetween(
                                                 now, next_execution.value())),
                               1000.0)))
                         : 1000;
    VLOG(5) << "Timeout: " << timeout_ms << " has value "
            << (next_execution.has_value() ? "yes" : "no");
    if (poll(fds, handler_.size(), timeout_ms) == -1) {
      CHECK_EQ(errno, EINTR) << "poll failed: " << strerror(errno);
      options.on_signals();
      return;
    }
    for (size_t i = 0; i < handler_.size(); i++)
      if (fds[i].revents & (POLLIN | POLLPRI | POLLHUP))
        handler_[i].second(fds[i].revents);
  }
};
}  // namespace

ExecutionEnvironment::ExecutionEnvironment(ExecutionEnvironmentOptions options)
    : options_(std::move(options)) {}

void ExecutionEnvironment::Run() {
  while (!options_.stop_check()) {
    IterationHandlerImpl handler;
    options_.on_iteration(handler);
    handler.Run(options_);
  }
}
}  // namespace afc::infrastructure::execution