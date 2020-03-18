#ifndef __AFC_EDITOR_NOTIFICATION_H__
#define __AFC_EDITOR_NOTIFICATION_H__

#include <condition_variable>
#include <mutex>

namespace afc::editor {
// This class is thread-safe.
class Notification {
 public:
  void Notify();
  bool HasBeenNotified() const;
  void WaitForNotification() const;

 private:
  mutable std::mutex mutex_;
  mutable std::condition_variable condition_;
  enum class State { kNotified, kPending };
  State state_ = State::kPending;
};
}  // namespace afc::editor
#endif  // __AFC_EDITOR_NOTIFICATION_H__
