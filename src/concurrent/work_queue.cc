#include "src/concurrent/work_queue.h"

#include "src/futures/delete_notification.h"
#include "src/infrastructure/time.h"
#include "src/tests/tests.h"

namespace afc::concurrent {
using futures::DeleteNotification;
using infrastructure::Now;
using infrastructure::SecondsBetween;
using language::MakeNonNullShared;
using language::NonNull;

/* static */ NonNull<std::shared_ptr<WorkQueue>> WorkQueue::New() {
  return MakeNonNullShared<WorkQueue>(ConstructorAccessTag());
}

WorkQueue::WorkQueue(ConstructorAccessTag) {}

void WorkQueue::Schedule(std::function<void()> callback) {
  CHECK(callback != nullptr);
  ScheduleAt(Now(), std::move(callback));
}

void WorkQueue::ScheduleAt(struct timespec time,
                           std::function<void()> callback) {
  CHECK(callback != nullptr);
  data_.lock()->callbacks.push({std::move(time), std::move(callback)});
  schedule_observers_.Notify();
}

void WorkQueue::Execute() {
  std::vector<std::function<void()>> callbacks_ready;
  auto start = Now();
  data_.lock([&callbacks_ready](MutableData& data) {
    VLOG(5) << "Executing work queue: callbacks: " << data.callbacks.size();
    while (!data.callbacks.empty() && data.callbacks.top().time < Now()) {
      callbacks_ready.push_back(std::move(data.callbacks.top().callback));
      data.callbacks.pop();
    }
  });

  for (auto& callback : callbacks_ready) {
    callback();
    // We could assign nullptr to `callback` in order to allow it to be deleted.
    // However, we prefer to only let them be deleted at the end, before we
    // return, in case they are the only thing keeping the work queue alive.
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
const bool work_queue_tests_registration = tests::Register(
    L"WorkQueue",
    {{.name = L"CallbackKeepsWorkQueueAlive", .runs = 100, .callback = [] {
        auto delete_notification = std::make_unique<DeleteNotification>();
        DeleteNotification::Value done =
            delete_notification->listenable_value();
        NonNull<WorkQueue*> work_queue_raw = [&delete_notification] {
          NonNull<std::shared_ptr<WorkQueue>> work_queue = WorkQueue::New();
          work_queue->Schedule(
              [work_queue] { LOG(INFO) << "First callback starts"; });
          work_queue->Schedule([&delete_notification] {
            LOG(INFO) << "Second callback starts";
            delete_notification = nullptr;
          });
          LOG(INFO) << "Execute.";
          return work_queue.get();
        }();
        // We know it hasn't been deleted since it contains a reference to
        // itself (in the first scheduled callback).
        work_queue_raw->Execute();
        while (!done->has_value()) sleep(0.01);
      }}});

const bool work_queue_channel_tests_registration = tests::Register(
    L"WorkQueueChannel",
    {{.name = L"CreateAndDestroy",
      .callback =
          [] {
            WorkQueueChannel<int>(
                WorkQueue::New(), [](int) {},
                WorkQueueChannelConsumeMode::kAll);
          }},
     // Creates a channel with consume mode kAll and pushes a few values. It
     // simulates that the work_queue executes in a somewhat random manner.
     {.name = L"SimpleConsumeAll",
      .callback =
          [] {
            std::vector<int> values;
            NonNull<std::shared_ptr<WorkQueue>> work_queue = WorkQueue::New();
            WorkQueueChannel<int> channel(
                work_queue, [&](int value) { values.push_back(value); },
                WorkQueueChannelConsumeMode::kAll);
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
            WorkQueueChannel<int> channel(
                work_queue, [&](int value) { values.push_back(value); },
                WorkQueueChannelConsumeMode::kLastAvailable);
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
            auto channel = std::make_unique<WorkQueueChannel<int>>(
                work_queue, [&](int value) { values.push_back(value); },
                WorkQueueChannelConsumeMode::kAll);
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
        auto channel = std::make_unique<WorkQueueChannel<int>>(
            work_queue, [&](int value) { values.push_back(value); },
            WorkQueueChannelConsumeMode::kLastAvailable);
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
}  // namespace afc::concurrent
