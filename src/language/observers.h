#ifndef __AFC_EDITOR_OBSERVERS_H__
#define __AFC_EDITOR_OBSERVERS_H__

#include <glog/logging.h>

#include <functional>
#include <memory>
#include <optional>

#include "src/concurrent/protected.h"
#include "src/futures/futures.h"

namespace afc::language {
class Observable {
 public:
  virtual ~Observable() {}

  // TODO(trivial, 2026-05-02): Get rid of the `k` prefix.
  enum class State { kExpired, kAlive };
  using Observer = std::move_only_function<State()>;

  // Why const? Because adding an observer doesn't modify the observable object.
  virtual void Add(Observer observer) const = 0;
};

// This class is thread-safe.
class Observers : public Observable {
 public:
  void Add(Observer observer) const override;

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
  futures::Value<EmptyValue> NewFuture() const;

  template <typename P, typename Callable>
  static Observer LockingObserver(std::weak_ptr<P> data, Callable callable) {
    return [data, callable] {
      auto shared_data = data.lock();
      if (shared_data == nullptr) return State::kExpired;
      callable(*shared_data);
      return State::kAlive;
    };
  }

  static Observer Once(OnceOnlyFunction<void()> observer) {
    return [observer = std::move(observer)] mutable {
      std::move(observer)();
      return State::kExpired;
    };
  }

 private:
  concurrent::Protected<std::vector<Observer>> observers_;

  // `Add` only adds to `new_observers_`, and it is the job of `Notify` to merge
  // those back into `observers_`. We do this so that observers can call `Add`
  // without deadlocking. We never hold both locks concurrently.
  //
  // This is mutable so that `Add` can be const.
  mutable concurrent::Protected<std::vector<Observer>> new_observers_;

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
  concurrent::Protected<NotifyState> notify_state_ = NotifyState::kIdle;
};

template <typename Value>
class ObservableValue : public Observable {
 public:
  static ObservableValue FromFuture(Value initial_value,
                                    futures::Value<Value> future_value) {
    ObservableValue output(std::move(initial_value));
    future_value.SetConsumer([data = output.data_](Value final_value) {
      data->Set(std::move(final_value));
    });
    return output;
  }

  ObservableValue() : ObservableValue(std::nullopt) {}

  explicit ObservableValue(Value value)
      : data_(language::MakeNonNullShared<Data>(
            Data{.value = concurrent::Protected<Value>(std::move(value))})) {}

  ObservableValue(const Observable&) = delete;

  void Set(Value value) { data_->Set(std::move(value)); }

  Value Get() const {
    return data_->value.lock([](const Value& value) { return value; });
  }

  // Adds a callback that will be updated whenever the value changes.
  void Add(Observers::Observer observer) const override {
    if (observer() == State::kAlive) data_->observers.Add(std::move(observer));
  }

  // The future returned ignores previous calls to Set (i.e., only gets notified
  // on the next call).
  futures::Value<EmptyValue> NewFuture() const {
    return data_->observers.NewFuture();
  }

 private:
  struct Data {
    Observers observers = Observers();
    concurrent::Protected<Value> value;

    void Set(Value next_value) {
      value.lock([&](Value& storage) {
        if (storage == next_value) return;  // Optimization.
        storage = std::move(next_value);
      });
      observers.Notify();
    }
  };
  NonNull<std::shared_ptr<Data>> data_;
};

}  // namespace afc::language
#endif  //__AFC_EDITOR_OBSERVERS_H__
