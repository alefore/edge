#ifndef __AFC_CONCURRENT_WORK_QUEUE_H__
#define __AFC_CONCURRENT_WORK_QUEUE_H__

#include <glog/logging.h>

#include <condition_variable>
#include <functional>
#include <mutex>
#include <queue>
#include <vector>

#include "src/concurrent/protected.h"
#include "src/futures/futures.h"
#include "src/infrastructure/time.h"
#include "src/language/observers.h"
#include "src/language/once_only_function.h"
#include "src/language/safe_types.h"
#include "src/math/decaying_counter.h"

namespace afc::concurrent {
// Contains a list of callbacks that will be executed later, at some point
// shortly before the Editor attempts to sleep waiting for IO (in the main
// loop). If this isn't empty, the main loop will actually skip the sleep and
// continue running.
//
// One of the uses of this is for long running operations that can't be executed
// in background threads. They periodically interrupt themselves and insert
// their continuations here. Edge flushes this to advance their work. This
// allows them to run without preventing Edge from handling input from the user.
//
// Another use is to ensure that a given execution (such as updating the syntax
// tree) only happens in "batches", after a set of operations has been applied
// to the buffer (rather than having to schedule many redundant runs, e.g., when
// input is being gradually read from a file).
//
// This class is thread-safe.
class WorkQueue : public std::enable_shared_from_this<WorkQueue> {
 public:
  struct ConstructorAccessTag {
   private:
    ConstructorAccessTag() = default;
    friend WorkQueue;
  };

  static language::NonNull<std::shared_ptr<WorkQueue>> New();

  explicit WorkQueue(ConstructorAccessTag);

  struct Callback {
    infrastructure::Time time = infrastructure::Now();
    language::OnceOnlyFunction<void()> callback = [] {};
  };

  void Schedule(Callback callback);

  futures::Value<language::EmptyValue> Wait(infrastructure::Time time);

  // Takes all the scheduled callbacks at a time in the past and executes them.
  // Any new callbacks that they transitively schedule may not (and typically
  // won't) be executed.
  void Execute();
  void Execute(std::function<infrastructure::Time()> clock);

  template <typename Object>
  void DeleteLater(infrastructure::Time time, Object object) {
    Schedule({.time = time, .callback = [object = std::move(object)] {}});
  }

  // Returns the time at which the earliest callback wants to run, or nullopt if
  // there are no pending callbacks.
  std::optional<infrastructure::Time> NextExecution();

  // Returns a value between 0.0 and 1.0 that indicates how much time this
  // WorkQueue is spending running callbacks, recently.
  double RecentUtilization() const;

  language::Observable& OnSchedule();

 private:
  struct MutableData {
    using Queue = std::priority_queue<
        Callback, std::vector<Callback>,
        std::function<bool(const Callback&, const Callback&)>>;

    // This is a priority queue. The elements are ~sorted by `Callback::time` in
    // descending order (last element is the next one that should execute). This
    // isn't a full order, but is a heap order.
    std::vector<Callback> callbacks;

    // This is used to track the percentage of time spent executing (seconds per
    // second).
    math::DecayingCounter execution_seconds = math::DecayingCounter(1.0);
  };
  Protected<MutableData> data_;
  language::Observers schedule_observers_;
};

// Represents the "writing" end of a channel: grants the ability to push items,
// to be consumed by ... something.
template <typename T>
class Channel {
 public:
  virtual ~Channel() = default;
  virtual void Push(T value) = 0;
};

// Executes `consume_callback` directly with all values received.
template <typename T>
class ChannelAll : public Channel<T> {
 public:
  ChannelAll(std::function<void(T t)> consume_callback)
      : consume_callback_(std::move(consume_callback)) {
    CHECK(consume_callback_ != nullptr);
  }

  void Push(T value) override { consume_callback_(std::move(value)); }

 private:
  const std::function<void(T t)> consume_callback_;
};

// Schedule processing of work through `schedule`, feeding it callbacks that
// represent invocations to `consume_callback`. If multiple calls to `Push`
// happen before `consume_callback` gets a chance to run, only runs
// `consume_callback` with the last value received.
//
// Obviously, because of possible races, there are no guarrantees, so this
// optimization is applied in a best-effort manner.
template <typename T>
class ChannelLast : public Channel<T> {
 public:
  ChannelLast(std::function<void(std::function<void()>)> schedule,
              std::function<void(T t)> consume_callback)
      : schedule_(std::move(schedule)),
        data_(language::MakeNonNullShared<Data>(std::move(consume_callback))) {
    CHECK(data_->consume_callback != nullptr);
  }

  void Push(T value) override {
    auto value_lock = data_->value.lock();
    bool already_scheduled = value_lock->has_value();
    *value_lock = std::move(value);
    value_lock = nullptr;
    if (already_scheduled) return;

    schedule_([data = data_] {
      std::optional<T> optional_value;
      data->value.lock()->swap(optional_value);
      CHECK(optional_value.has_value());
      data->consume_callback(*optional_value);
    });
  }

 private:
  const std::function<void(std::function<void()>)> schedule_;

  // To enable deletion of the channel before the callbacks it schedules in
  // work_queue have executed, we move the fields that such callbacks depend on
  // to a structure that we put in a shared_ptr.
  struct Data {
    Data(std::function<void(T t)> input_consume_callback)
        : consume_callback(std::move(input_consume_callback)) {}

    const std::function<void(T t)> consume_callback;

    Protected<std::optional<T>> value;
  };
  const language::NonNull<std::shared_ptr<Data>> data_;
};

std::function<void(std::function<void()>)> WorkQueueScheduler(
    language::NonNull<std::shared_ptr<WorkQueue>> work_queue);
}  // namespace afc::concurrent
#endif  // __AFC_CONCURRENT_WORK_QUEUE_H__
