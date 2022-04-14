#include "src/observers.h"

namespace afc::editor {
void Observers::Add(Observers::Observer observer) {
  observers_.lock()->push_back(std::move(observer));
}

void Observers::Notify() {
  auto observers = observers_.lock();
  bool expired_observers = false;
  for (auto& o : *observers) {
    switch (o()) {
      case State::kAlive:
        break;
      case State::kExpired:
        o = nullptr;
        expired_observers = true;
    }
  }
  if (expired_observers)
    observers->erase(std::remove(observers->begin(), observers->end(), nullptr),
                     observers->end());
}

futures::Value<EmptyValue> Observers::NewFuture() {
  futures::Future<EmptyValue> output;
  Add(Observers::Once(
      [consumer = std::move(output.consumer)] { consumer(EmptyValue()); }));
  return std::move(output.value);
}
}  // namespace afc::editor
