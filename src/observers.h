#ifndef __AFC_EDITOR_OBSERVERS_H__
#define __AFC_EDITOR_OBSERVERS_H__

#include <glog/logging.h>

#include <functional>
#include <memory>

namespace afc::editor {

class Observers {
 public:
  enum class State { kExpired, kAlive };
  using Observer = std::function<State()>;

  void Add(Observer observer);

  // Will remove expired observers from the container.
  void Notify();

  template <typename P, typename Callable>
  static Observer LockingObserver(std::weak_ptr<P> data, Callable callable) {
    return [data, callable] {
      auto shared_data = data.lock();
      if (shared_data == nullptr) return State::kExpired;
      callable(*shared_data);
      return State::kAlive;
    };
  }

 private:
  std::vector<Observer> observers_;
};

}  // namespace afc::editor
#endif  //__AFC_EDITOR_OBSERVERS_H__
