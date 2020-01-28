#include "src/work_queue.h"

namespace afc::editor {
WorkQueue::WorkQueue(std::function<void()> schedule_listener)
    : schedule_listener_(std::move(schedule_listener)) {
  CHECK(schedule_listener_ != nullptr);
}

void WorkQueue::Schedule(std::function<void()> callback) {
  std::unique_lock<std::mutex> lock(mutex_);
  callbacks_.push_back(std::move(callback));
  schedule_listener_();
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
