#include "src/concurrent/notification.h"

namespace afc::concurrent {
void Notification::Notify() {
  state_.lock([](State& state) { state = State::kNotified; });
}

bool Notification::HasBeenNotified() const {
  return state_.lock(
      [](const State& state) { return state == State::kNotified; });
}

}  // namespace afc::concurrent
