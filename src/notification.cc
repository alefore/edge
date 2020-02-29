#include "src/notification.h"

namespace afc::editor {
void Notification::Notify() {
  std::unique_lock<std::mutex> lock(mutex_);
  notified_ = Notified::kYes;
}

bool Notification::HasBeenNotified() const {
  std::unique_lock<std::mutex> lock(mutex_);
  return notified_ == Notified::kYes;
}

}  // namespace afc::editor
