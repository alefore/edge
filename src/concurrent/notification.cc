#include "src/concurrent/notification.h"

namespace afc::editor {
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

void Notification::WaitForNotification() const {
  return state_.wait(
      [](const State& state) { return state == State::kNotified; });
}

}  // namespace afc::editor
