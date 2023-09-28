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
class WorkQueue {
 public:
  struct ConstructorAccessTag {
   private:
    ConstructorAccessTag() = default;
    friend WorkQueue;
  };

  static language::NonNull<std::shared_ptr<WorkQueue>> New();

  explicit WorkQueue(ConstructorAccessTag);

  struct Callback {
    struct timespec time = infrastructure::Now();
    std::function<void()> callback;
  };

  void Schedule(Callback callback);

  futures::Value<language::EmptyValue> Wait(struct timespec time);

  // Takes all the scheduled callbacks at a time in the past and executes them.
  // Any new callbacks that they transitively schedule may not (and typically
  // won't) be executed.
  void Execute();

  template <typename CopyableObject>
  void DeleteLater(struct timespec time, CopyableObject object) {
    Schedule(time, [object] {});
  }

  // Returns the time at which the earliest callback wants to run, or nullopt if
  // there are no pending callbacks.
  std::optional<struct timespec> NextExecution();

  // Returns a value between 0.0 and 1.0 that indicates how much time this
  // WorkQueue is spending running callbacks, recently.
  double RecentUtilization() const;

  language::Observable& OnSchedule();

 private:
  struct MutableData {
    using Queue = std::priority_queue<
        Callback, std::vector<Callback>,
        std::function<bool(const Callback&, const Callback&)>>;

    Queue callbacks = Queue([](const Callback& a, const Callback& b) {
      return !(a.time < b.time);
    });

    // This is used to track the percentage of time spent executing (seconds per
    // second).
    math::DecayingCounter execution_seconds = math::DecayingCounter(1.0);
  };
  Protected<MutableData> data_;
  language::Observers schedule_observers_;
};

enum class WorkQueueChannelConsumeMode {
  // consume_callback will execute on all values given to Push, in order.
  kAll,
  // If multiple values are pushed quickly (before the work queue can consume
  // some of them), we'd rather skip intermediate values and only process the
  // very last available value.
  //
  // Obviously, because of possible races, there are no guarrantees, so this
  // optimization is applied in a best-effort manner.
  kLastAvailable
};

// Schedules in a work_queue execution of consume_callback for the values given
// to WorkQueueChannel::Push.
//
// A WorkQueueChannel can be deleted before the callbacks it schedules in
// work_queue have executed.
template <typename T>
class WorkQueueChannel {
 public:
  WorkQueueChannel(language::NonNull<std::shared_ptr<WorkQueue>> work_queue,
                   std::function<void(T t)> consume_callback,
                   WorkQueueChannelConsumeMode consume_mode)
      : work_queue_(std::move(work_queue)),
        consume_mode_(consume_mode),
        data_(language::MakeNonNullShared<Data>(std::move(consume_callback))) {
    CHECK(data_->consume_callback != nullptr);
  }

  const language::NonNull<std::shared_ptr<WorkQueue>>& work_queue() const {
    return work_queue_;
  }

  WorkQueueChannelConsumeMode consume_mode() const { return consume_mode_; }

  void Push(T value) {
    switch (consume_mode_) {
      case WorkQueueChannelConsumeMode::kAll:
        work_queue_->Schedule(WorkQueue::Callback{
            .callback = [data = data_, value = std::move(value)]() mutable {
              data->consume_callback(std::move(value));
            }});
        break;

      case WorkQueueChannelConsumeMode::kLastAvailable:
        auto value_lock = data_->value.lock();
        bool already_scheduled = value_lock->has_value();
        *value_lock = std::move(value);
        value_lock = nullptr;
        if (already_scheduled) return;

        work_queue_->Schedule(WorkQueue::Callback{.callback = [data = data_] {
          std::optional<T> optional_value;
          data->value.lock()->swap(optional_value);
          CHECK(optional_value.has_value());
          data->consume_callback(*optional_value);
        }});
        break;
    }
  }

 private:
  const language::NonNull<std::shared_ptr<WorkQueue>> work_queue_;
  const WorkQueueChannelConsumeMode consume_mode_;

  // To enable deletion of the channel before the callbacks it schedules in
  // work_queue have executed, we move the fields that such callbacks depend on
  // to a structure that we put in a shared_ptr.
  struct Data {
    Data(std::function<void(T t)> input_consume_callback)
        : consume_callback(std::move(input_consume_callback)) {}

    const std::function<void(T t)> consume_callback;

    // Only used when consume_mode_ is kLastAvailable.
    Protected<std::optional<T>> value;
  };
  const language::NonNull<std::shared_ptr<Data>> data_;
};
}  // namespace afc::concurrent
#endif  // __AFC_CONCURRENT_WORK_QUEUE_H__
