#include "src/notification.h"

namespace afc::editor {
void Notification::Notify() {
  std::unique_lock<std::mutex> lock(mutex_);
  state_ = State::kNotified;
  condition_.notify_all();
}

bool Notification::HasBeenNotified() const {
  std::unique_lock<std::mutex> lock(mutex_);
  return state_ == State::kNotified;
}

void Notification::WaitForNotification() const {
  std::unique_lock<std::mutex> lock(mutex_);
  condition_.wait(lock, [&] { return state_ == State::kNotified; });
}

}  // namespace afc::editor
