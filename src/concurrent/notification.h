#ifndef __AFC_EDITOR_NOTIFICATION_H__
#define __AFC_EDITOR_NOTIFICATION_H__

#include "src/concurrent/protected.h"

namespace afc::concurrent {
// This class is thread-safe.
class Notification {
 public:
  void Notify();
  bool HasBeenNotified() const;

 private:
  enum class State { kNotified, kPending };
  ProtectedWithCondition<State> state_ =
      ProtectedWithCondition<State>(State::kPending);
};
}  // namespace afc::concurrent
#endif  // __AFC_EDITOR_NOTIFICATION_H__
