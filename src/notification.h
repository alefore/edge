#ifndef __AFC_EDITOR_NOTIFICATION_H__
#define __AFC_EDITOR_NOTIFICATION_H__

#include "src/protected.h"

namespace afc::editor {
// This class is thread-safe.
class Notification {
 public:
  void Notify();
  bool HasBeenNotified() const;
  void WaitForNotification() const;

 private:
  enum class State { kNotified, kPending };
  ProtectedWithCondition<State> state_ =
      ProtectedWithCondition<State>(State::kPending);
};
}  // namespace afc::editor
#endif  // __AFC_EDITOR_NOTIFICATION_H__
