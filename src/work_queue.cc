#include "src/work_queue.h"

#include "src/tests/tests.h"
#include "src/time.h"

namespace afc::editor {
/* static */ std::shared_ptr<WorkQueue> WorkQueue::New(
    std::function<void()> schedule_listener) {
  return std::make_shared<WorkQueue>(ConstructorAccessTag(),
                                     std::move(schedule_listener));
}

WorkQueue::WorkQueue(ConstructorAccessTag,
                     std::function<void()> schedule_listener)
    : schedule_listener_(std::move(schedule_listener)),
      callbacks_([](const Callback& a, const Callback& b) {
        return !(a.time < b.time);
      }) {
  CHECK(schedule_listener_ != nullptr);
}

void WorkQueue::Schedule(std::function<void()> callback) {
  struct timespec now;
  CHECK_NE(clock_gettime(0, &now), -1);
  ScheduleAt(std::move(now), std::move(callback));
}

void WorkQueue::ScheduleAt(struct timespec time,
                           std::function<void()> callback) {
  std::unique_lock<std::mutex> lock(mutex_);
  callbacks_.push(Callback{std::move(time), std::move(callback)});
  auto listener = schedule_listener_;
  lock.unlock();
  listener();
}

void WorkQueue::Execute() {
  mutex_.lock();
  auto start = Now();
  VLOG(5) << "Executing work queue: callbacks: " << callbacks_.size();
  std::vector<std::function<void()>> callbacks_ready;
  while (!callbacks_.empty() && callbacks_.top().time < Now()) {
    callbacks_ready.push_back(std::move(callbacks_.top().callback));
    callbacks_.pop();
  }

  for (auto& callback : callbacks_ready) {
    mutex_.unlock();
    callback();
    // We could assign nullptr to `callback` in order to allow it to be deleted.
    // However, we prefer to only let them be deleted at the end, before we
    // return, in case they are the only thing keeping the work queue alive.
    mutex_.lock();
    auto end = Now();
    execution_seconds_.IncrementAndGetEventsPerSecond(
        SecondsBetween(start, end));
    start = end;
  }
  mutex_.unlock();
}

std::optional<struct timespec> WorkQueue::NextExecution() {
  return callbacks_.empty()
             ? std::nullopt
             : std::optional<struct timespec>(callbacks_.top().time);
}

double WorkQueue::RecentUtilization() const {
  std::unique_lock<std::mutex> lock(mutex_);
  return execution_seconds_.GetEventsPerSecond();
}

void WorkQueue::SetScheduleListener(std::function<void()> schedule_listener) {
  std::unique_lock<std::mutex> lock(mutex_);
  schedule_listener_ = std::move(schedule_listener);
}

namespace {
const bool work_queue_channel_tests_registration = tests::Register(
    L"WorkQueueChannelTests",
    {{.name = L"CreateAndDestroy",
      .callback =
          [] {
            WorkQueueChannel<int>(
                WorkQueue::New([] {}), [](int) {},
                WorkQueueChannelConsumeMode::kAll);
          }},
     // Creates a channel with consume mode kAll and pushes a few values. It
     // simulates that the work_queue executes in a somewhat random manner.
     {.name = L"SimpleConsumeAll",
      .callback =
          [] {
            std::vector<int> values;
            auto work_queue = WorkQueue::New([] {});
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
            auto work_queue = WorkQueue::New([] {});
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
            auto work_queue = WorkQueue::New([] {});
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
        auto work_queue = WorkQueue::New([] {});
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
}  // namespace afc::editor
