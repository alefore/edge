#include "src/work_queue.h"

namespace afc::editor {
void WorkQueue::Schedule(std::function<void()> callback) {
  std::unique_lock<std::mutex> lock(mutex_);
  callbacks_.push_back(std::move(callback));
}

void WorkQueue::Execute() {
  std::vector<std::function<void()>> callbacks;

  mutex_.lock();
  callbacks.swap(callbacks_);
  mutex_.unlock();

  VLOG(5) << "Executing work queue: " << callbacks.size();
  for (auto& c : callbacks) {
    c();
  }
}
WorkQueue::State WorkQueue::state() const {
  std::unique_lock<std::mutex> lock(mutex_);
  return callbacks_.empty() ? State::kIdle : State::kScheduled;
}

}  // namespace afc::editor
