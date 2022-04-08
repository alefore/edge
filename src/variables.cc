#include "src/variables.h"

namespace afc::editor {
void RunObservers(std::vector<VariableObserver>& observers) {
  bool expired_observers = false;
  for (auto& o : observers) {
    switch (o()) {
      case ObserverState::kAlive:
        break;
      case ObserverState::kExpired:
        o = nullptr;
        expired_observers = true;
    }
  }
  if (expired_observers)
    observers.erase(std::remove(observers.begin(), observers.end(), nullptr),
                    observers.end());
}
}  // namespace afc::editor
