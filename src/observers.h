#ifndef __AFC_EDITOR_OBSERVERS_H__
#define __AFC_EDITOR_OBSERVERS_H__

#include <glog/logging.h>

#include <functional>
#include <memory>
#include <optional>

#include "src/futures/futures.h"

namespace afc::editor {

class Observers {
 public:
  enum class State { kExpired, kAlive };
  using Observer = std::function<State()>;

  void Add(Observer observer);

  // Will remove expired observers from the container.
  void Notify();

  // Returns a future that gets notificed the next time that `Notify` is called.
  futures::Value<EmptyValue> NewFuture();

  template <typename P, typename Callable>
  static Observer LockingObserver(std::weak_ptr<P> data, Callable callable) {
    return [data, callable] {
      auto shared_data = data.lock();
      if (shared_data == nullptr) return State::kExpired;
      callable(*shared_data);
      return State::kAlive;
    };
  }

  static Observer Once(std::function<void()> observer) {
    return [observer = std::move(observer)] {
      observer();
      return State::kExpired;
    };
  }

 private:
  std::vector<Observer> observers_;
};

template <typename Value>
class Observable {
 public:
  Observable() : Observable(std::nullopt) {}
  explicit Observable(std::optional<Value> value) : value_(std::move(value)) {}
  Observable(const Observable&) = delete;

  void Set(Value value) {
    if (value_ == value) return;  // Optimization.
    value_ = std::move(value);
    observers_.Notify();
  }

  const std::optional<Value>& Get() const { return value_; }

  // Adds a callback that will be updated whenever the value changes.
  //
  // We will only notify the observers after `Get` returns a value.
  void Add(Observers::Observer observer) {
    if (value_.has_value()) observer();
    observers_.Add(std::move(observer));
  }

 private:
  std::optional<Value> value_;
  Observers observers_;
};

}  // namespace afc::editor
#endif  //__AFC_EDITOR_OBSERVERS_H__
