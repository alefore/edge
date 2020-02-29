#ifndef __AFC_EDITOR_NOTIFICATION_H__
#define __AFC_EDITOR_NOTIFICATION_H__

#include <mutex>

namespace afc::editor {
// This class is thread-safe.
class Notification {
 public:
  void Notify();
  bool HasBeenNotified() const;

 private:
  mutable std::mutex mutex_;
  enum class Notified { kYes, kNo };
  Notified notified_ = Notified::kNo;
};
}  // namespace afc::editor
#endif  // __AFC_EDITOR_NOTIFICATION_H__
