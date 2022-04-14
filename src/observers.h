#ifndef __AFC_EDITOR_OBSERVERS_H__
#define __AFC_EDITOR_OBSERVERS_H__

#include <glog/logging.h>

#include <functional>
#include <memory>
#include <optional>

#include "src/futures/futures.h"
#include "src/protected.h"

namespace afc::editor {

// This class is thread-safe.
class Observers {
 public:
  enum class State { kExpired, kAlive };
  using Observer = std::function<State()>;

  void Add(Observer observer);

  // Notify is fully reentrant.
  //
  // Notify will remove expired observers from the container.
  //
  // When Notify is called concurrently (by different threads or from one of the
  // observers), some of them may return before the notifications happen. We
  // guarrantee that all observers will be notified after the start of the last
  // call to Notify (but may actually execute the observers fewer times than the
  // number of calls to Notify).
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
  Protected<std::vector<Observer>> observers_;

  // `Add` only adds to `new_observers_`, and it is the job of `Notify` to merge
  // those back into `observers_`. We do this so that observers can call `Add`
  // without deadlocking. We never require both locks to be held concurrently.
  Protected<std::vector<Observer>> new_observers_;

  // This allow us to make Notify reentrant.
  enum class NotifyState {
    // Notify is not running. The first call should actually work.
    kIdle,
    // A call to Notify is running; once it finishes, it should return.
    kRunning,
    // A call to Notify happened while Notify was running. When the thread that
    // is delivering notifications finishes, it should switch back to kRunning
    // and start delivering notifications again.
    kRunningAndScheduled
  };
  Protected<NotifyState> notify_state_ = NotifyState::kIdle;
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
