#include "src/concurrent/work_queue.h"

#include <chrono>
#include <thread>

#include "src/futures/delete_notification.h"
#include "src/infrastructure/time.h"
#include "src/tests/tests.h"

using afc::infrastructure::Now;
using afc::infrastructure::SecondsBetween;
using afc::language::EmptyValue;
using afc::language::MakeNonNullShared;
using afc::language::NonNull;
using afc::language::OnceOnlyFunction;

namespace afc::concurrent {
/* static */ NonNull<std::shared_ptr<WorkQueue>> WorkQueue::New() {
  return MakeNonNullShared<WorkQueue>(ConstructorAccessTag());
}

WorkQueue::WorkQueue(ConstructorAccessTag) {}

void WorkQueue::Schedule(WorkQueue::Callback callback) {
  data_.lock()->callbacks.push(std::move(callback));
  schedule_observers_.Notify();
}

futures::Value<EmptyValue> WorkQueue::Wait(struct timespec time) {
  futures::Future<EmptyValue> value;
  Schedule({.time = time,
            .callback = OnceOnlyFunction<void()>(
                [consumer = std::move(value.consumer)] {
                  consumer(EmptyValue());
                })});
  return std::move(value.value);
}

void WorkQueue::Execute() {
  std::vector<OnceOnlyFunction<void()>> callbacks_ready;
  auto start = Now();
  data_.lock([&callbacks_ready](MutableData& data) {
    VLOG(5) << "Executing work queue: callbacks: " << data.callbacks.size();
    while (!data.callbacks.empty() && data.callbacks.top().time < Now()) {
      callbacks_ready.push_back(std::move(data.callbacks.top().callback));
      data.callbacks.pop();
    }
  });

  if (callbacks_ready.empty()) return;

  // Make sure we stay alive until all callbacks have run.
  const std::shared_ptr<WorkQueue> shared_this = shared_from_this();
  for (auto& callback : callbacks_ready) {
    std::move(callback)();
    auto end = Now();
    data_.lock()->execution_seconds.IncrementAndGetEventsPerSecond(
        SecondsBetween(start, end));
    start = end;
  }
}

std::optional<struct timespec> WorkQueue::NextExecution() {
  return data_.lock([](MutableData& data) {
    return data.callbacks.empty()
               ? std::nullopt
               : std::optional<struct timespec>(data.callbacks.top().time);
  });
}

double WorkQueue::RecentUtilization() const {
  return data_.lock([](const MutableData& data) {
    return data.execution_seconds.GetEventsPerSecond();
  });
}

language::Observable& WorkQueue::OnSchedule() { return schedule_observers_; }

namespace {
using futures::DeleteNotification;

const bool work_queue_tests_registration = tests::Register(
    L"WorkQueue",
    {{.name = L"CallbackKeepsWorkQueueAlive", .runs = 100, .callback = [] {
        auto delete_notification = std::make_unique<DeleteNotification>();
        DeleteNotification::Value done =
            delete_notification->listenable_value();
        NonNull<WorkQueue*> work_queue_raw = [&delete_notification] {
          NonNull<std::shared_ptr<WorkQueue>> work_queue = WorkQueue::New();
          work_queue->Schedule(WorkQueue::Callback{.callback = [work_queue] {
            LOG(INFO) << "First callback starts";
          }});
          work_queue->Schedule(
              WorkQueue::Callback{.callback = [&delete_notification] {
                LOG(INFO) << "Second callback starts";
                delete_notification = nullptr;
              }});
          LOG(INFO) << "Execute.";
          return work_queue.get();
        }();
        // We know it hasn't been deleted since it contains a reference to
        // itself (in the first scheduled callback).
        work_queue_raw->Execute();
        for (size_t iterations = 0; !done.has_value(); ++iterations) {
          CHECK_LT(iterations, 1000ul);
          std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
      }}});

const bool work_queue_channel_tests_registration = tests::Register(
    L"Channel",
    {{.name = L"All::CreateAndDestroy",
      .callback = [] { ChannelAll<int>([](int) {}); }},
     {.name = L"Latest::CreateAndDestroy",
      .callback =
          [] {
            ChannelLast<int>(WorkQueueScheduler(WorkQueue::New()), [](int) {});
          }},
     // Creates a channel with consume mode kAll and pushes a few values. It
     // simulates that the work_queue executes in a somewhat random manner.
     {.name = L"SimpleConsumeAll",
      .callback =
          [] {
            std::vector<int> values;
            NonNull<std::shared_ptr<WorkQueue>> work_queue = WorkQueue::New();
            ChannelAll<int> channel([&values, work_queue](int value) {
              work_queue->Schedule(WorkQueue::Callback{
                  .callback = [&values, value] { values.push_back(value); }});
            });
            channel.Push(0);
            CHECK_EQ(values.size(), 0ul);
            work_queue->Execute();
            CHECK_EQ(values.size(), 1ul);
            channel.Push(1);
            channel.Push(2);
            channel.Push(3);
            CHECK_EQ(values.size(), 1ul);
            work_queue->Execute();
            CHECK_EQ(values.size(), 4ul);
            channel.Push(4);
            channel.Push(5);
            CHECK_EQ(values.size(), 4ul);
            work_queue->Execute();
            CHECK_EQ(values.size(), 6ul);

            for (int i = 0; i < 6; i++) {
              CHECK_EQ(values[i], i);
            }
          }},
     // Creates a channel with consume mode kLastAvailableAll and pushes a few
     // values. It simulates that the work_queue executes in a somewhat random
     // manner.
     {.name = L"SimpleConsumeLastAvailable",
      .callback =
          [] {
            std::vector<int> values;
            auto work_queue = WorkQueue::New();
            ChannelLast<int> channel(
                WorkQueueScheduler(work_queue),
                [&](int value) { values.push_back(value); });
            channel.Push(0);
            CHECK_EQ(values.size(), 0ul);
            work_queue->Execute();
            CHECK_EQ(values.size(), 1ul);
            channel.Push(1);
            channel.Push(2);
            channel.Push(3);
            CHECK_EQ(values.size(), 1ul);
            work_queue->Execute();
            CHECK_EQ(values.size(), 2ul);
            channel.Push(4);
            channel.Push(5);
            CHECK_EQ(values.size(), 2ul);
            work_queue->Execute();
            CHECK_EQ(values.size(), 3ul);

            CHECK_EQ(values[0], 0);
            CHECK_EQ(values[1], 3);
            CHECK_EQ(values[2], 5);
          }},
     // Validates that a channel witk kAll can be deleted before its callbacks
     // execute.
     {.name = L"AllChannelDeleteBeforeExecute",
      .callback =
          [] {
            std::vector<int> values;
            auto work_queue = WorkQueue::New();
            auto channel = std::make_unique<ChannelAll<int>>(
                [&values, work_queue](int value) {
                  work_queue->Schedule({.callback = [&values, value] {
                    values.push_back(value);
                  }});
                });
            channel->Push(0);
            channel->Push(1);
            channel->Push(2);
            channel = nullptr;

            CHECK_EQ(values.size(), 0ul);
            work_queue->Execute();
            CHECK_EQ(values.size(), 3ul);
            CHECK_EQ(values[0], 0);
            CHECK_EQ(values[1], 1);
            CHECK_EQ(values[2], 2);
          }},
     // Validates that a channel witk kLastAvailable can be deleted before its
     // callbacks execute.
     {.name = L"LastAvailableChannelDeleteBeforeExecute", .callback = [] {
        std::vector<int> values;
        auto work_queue = WorkQueue::New();
        auto channel = std::make_unique<ChannelLast<int>>(
            WorkQueueScheduler(work_queue),
            [&](int value) { values.push_back(value); });
        channel->Push(0);
        channel->Push(1);
        channel->Push(2);
        channel = nullptr;

        CHECK_EQ(values.size(), 0ul);
        work_queue->Execute();
        CHECK_EQ(values.size(), 1ul);
        CHECK_EQ(values[0], 2);
      }}});
}  // namespace

std::function<void(std::function<void()>)> WorkQueueScheduler(
    language::NonNull<std::shared_ptr<WorkQueue>> work_queue) {
  return [work_queue](std::function<void()> work) {
    work_queue->Schedule({.callback = std::move(work)});
  };
}
}  // namespace afc::concurrent
