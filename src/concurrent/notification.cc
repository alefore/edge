#include "src/concurrent/notification.h"

namespace afc::concurrent {
void Notification::Notify() {
  state_.lock([](State& state, std::condition_variable& condition) {
    state = State::kNotified;
    condition.notify_all();
  });
}

bool Notification::HasBeenNotified() const {
  return state_.lock([](const State& state, std::condition_variable&) {
    return state == State::kNotified;
  });
}

}  // namespace afc::concurrent
