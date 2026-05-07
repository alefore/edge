#include "src/child_process_tracker.h"

#include <glog/logging.h>

#include "src/language/lazy_string/single_line.h"

using afc::infrastructure::FileSystemDriver;
using afc::infrastructure::ProcessId;
using afc::language::EmptyValue;
using afc::language::Error;
using afc::language::NonNull;
using afc::language::PossibleError;
using afc::language::lazy_string::NonEmptySingleLine;
using afc::language::lazy_string::SingleLine;

namespace afc::editor {
std::optional<ProcessId> ChildProcessTracker::pid() const { return pid_; }

void ChildProcessTracker::set_pid(
    std::optional<infrastructure::ProcessId> pid) {
  CHECK(!pid_);
  pid_ = pid;
}

futures::Value<EmptyValue> ChildProcessTracker::WaitPid(
    NonNull<std::shared_ptr<FileSystemDriver>> file_system_driver) {
  if (!pid_) return EmptyValue{};

  return file_system_driver->WaitPid(pid_.value(), 0)
      .Transform([this](FileSystemDriver::WaitPidOutput waitpid_output)
                     -> futures::Value<PossibleError> {
        set_exit_status(waitpid_output.wstatus);
        return EmptyValue{};
      })
      .ConsumeErrors([](Error) { return EmptyValue{}; });
}

std::optional<int> ChildProcessTracker::exit_status() const {
  return exit_status_;
}

void ChildProcessTracker::set_exit_status(int status) {
  CHECK(pid_.has_value());
  exit_status_ = status;
  clock_gettime(0, &time_last_exit_);
  pid_ = std::nullopt;
}

struct timespec ChildProcessTracker::time_last_exit() const {
  CHECK(exit_status_);
  return time_last_exit_;
}

bool ChildProcessTracker::is_dirty() const {
  return pid_.has_value() ||
         (exit_status_.has_value() && (!WIFEXITED(exit_status_.value()) ||
                                       WEXITSTATUS(exit_status_.value()) != 0));
}

void ChildProcessTracker::GetFlags(
    std::map<BufferFlagKey, BufferFlagValue>& output) const {
  if (pid_) {
    output.insert({BufferFlagKey{SingleLine::Char<L'🟡'>()},
                   BufferFlagValue{NonEmptySingleLine{pid_->read()}}});
    return;
  }
  if (!exit_status_.has_value()) return;

  if (WIFEXITED(exit_status_.value())) {
    auto exit_status = WEXITSTATUS(exit_status_.value());
    if (exit_status == 0)
      output.insert(
          {BufferFlagKey{SingleLine::Char<L'🟢'>()}, BufferFlagValue{}});
    else
      output.insert({BufferFlagKey{SingleLine::Char<L'🔴'>()},
                     BufferFlagValue{NonEmptySingleLine(exit_status)}});
    return;
  }
  if (WIFSIGNALED(exit_status_.value())) {
    output.insert(
        {BufferFlagKey{SingleLine::Char<L'🟣'>()},
         BufferFlagValue{NonEmptySingleLine{WTERMSIG(exit_status_.value())}}});
    return;
  }
  output.insert({BufferFlagKey{SINGLE_LINE_CONSTANT(L"exit-status")},
                 BufferFlagValue{NonEmptySingleLine{exit_status_.value()}}});
}
}  // namespace afc::editor
