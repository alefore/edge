#include "src/work_queue.h"

#include "src/time.h"

namespace afc::editor {
WorkQueue::WorkQueue(std::function<void()> schedule_listener)
    : schedule_listener_(std::move(schedule_listener)),
      callbacks_([](const Callback& a, const Callback& b) {
        return !(a.time < b.time);
      }) {
  CHECK(schedule_listener_ != nullptr);
}

void WorkQueue::Schedule(std::function<void()> callback) {
  struct timespec now;
  CHECK_NE(clock_gettime(0, &now), -1);
  ScheduleAt(std::move(now), std::move(callback));
}

void WorkQueue::ScheduleAt(struct timespec time,
                           std::function<void()> callback) {
  std::unique_lock<std::mutex> lock(mutex_);
  callbacks_.push(Callback{std::move(time), std::move(callback)});
  lock.unlock();
  schedule_listener_();
}

void WorkQueue::Execute() {
  mutex_.lock();
  VLOG(5) << "Executing work queue: callbacks: " << callbacks_.size();
  while (!callbacks_.empty() && callbacks_.top().time < Now()) {
    auto callback = std::move(callbacks_.top().callback);
    callbacks_.pop();
    mutex_.unlock();
    callback();
    mutex_.lock();
  }
  mutex_.unlock();
}

std::optional<struct timespec> WorkQueue::NextExecution() {
  return callbacks_.empty()
             ? std::nullopt
             : std::optional<struct timespec>(callbacks_.top().time);
}

}  // namespace afc::editor
