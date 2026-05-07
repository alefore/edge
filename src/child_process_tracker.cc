#include "src/child_process_tracker.h"

#include <glog/logging.h>

#include "src/language/lazy_string/single_line.h"

using afc::infrastructure::ProcessId;
using afc::language::lazy_string::NonEmptySingleLine;
using afc::language::lazy_string::SingleLine;

namespace afc::editor {
std::optional<ProcessId> ChildProcessTracker::pid() const { return pid_; }

void ChildProcessTracker::set_pid(
    std::optional<infrastructure::ProcessId> pid) {
  CHECK(!pid_);
  pid_ = pid;
}

std::optional<int> ChildProcessTracker::exit_status() const {
  return exit_status_;
}

void ChildProcessTracker::set_exit_status(int status) {
  CHECK(pid_.has_value());
  exit_status_ = status;
  pid_ = std::nullopt;
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
