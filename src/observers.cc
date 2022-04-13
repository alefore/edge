#include "src/observers.h"

namespace afc::editor {
void Observers::Add(Observers::Observer observer) {
  observers_.push_back(std::move(observer));
}

void Observers::Notify() {
  bool expired_observers = false;
  for (auto& o : observers_) {
    switch (o()) {
      case State::kAlive:
        break;
      case State::kExpired:
        o = nullptr;
        expired_observers = true;
    }
  }
  if (expired_observers)
    observers_.erase(std::remove(observers_.begin(), observers_.end(), nullptr),
                     observers_.end());
}

futures::Value<EmptyValue> Observers::NewFuture() {
  futures::Future<EmptyValue> output;
  Add(Observers::Once(
      [consumer = std::move(output.consumer)] { consumer(EmptyValue()); }));
  return std::move(output.value);
}
}  // namespace afc::editor
