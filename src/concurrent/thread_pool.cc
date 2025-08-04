#include "src/concurrent/thread_pool.h"

#include "src/infrastructure/time_human.h"
#include "src/language/safe_types.h"
#include "src/tests/tests.h"

using afc::language::NonNull;

namespace afc::concurrent {
ThreadPool::ThreadPool(size_t size) : size_(size) {
  data_.lock([this](Data& data, std::condition_variable&) {
    for (size_t i = 0; i < size_; i++) {
      data.threads.push_back(std::thread([this]() { BackgroundThread(); }));
    }
  });
}

size_t ThreadPool::size() const { return size_; }

size_t ThreadPool::pending_work_units() const {
  return data_.lock([](const Data& data, std::condition_variable&) {
    return data.active_work + data.work.size();
  });
}

void ThreadPool::WaitForProgress() const {
  // TODO(trivial, 2025-08-03): Instead of waiting until there are fewer units,
  // wait instead until ... some progress is made. I think this requires adding
  // a variable.
  if (size_t pending = pending_work_units(); pending > 0) {
    LOG(INFO) << "Waiting with pending units: " << pending;
    data_.wait([pending](const Data& data) {
      LOG(INFO) << "Checking: active: " << data.active_work
                << ", scheduled: " << data.work.size();
      return data.active_work + data.work.size() < pending;
    });
  }
}

ThreadPool::~ThreadPool() {
  LOG(INFO) << "Starting destruction of ThreadPool.";
  std::vector<std::thread> threads;
  data_.lock([&threads](Data& data, std::condition_variable& condition) {
    CHECK(!data.shutting_down);
    data.shutting_down = true;
    condition.notify_all();
    threads.swap(data.threads);
  });
  LOG(INFO) << "Joining threads.";
  for (auto& t : threads) t.join();
  LOG(INFO) << "All threads are joined.";
}

void ThreadPool::Schedule(std::function<void()> work) {
  CHECK(work != nullptr);
  if (auto handler = tests::concurrent::GetGlobalHandler();
      handler != nullptr) {
    work = handler->Wrap(std::move(work));
  }
  data_.lock([&work](Data& data, std::condition_variable& condition) {
    CHECK(!data.shutting_down);
    data.work.push_back(std::move(work));
    condition.notify_one();
  });
}

void ThreadPool::BackgroundThread() {
  bool active_work_thread = false;
  while (true) {
    std::function<void()> callback;
    VLOG(8) << "BackgroundThread waits for work.";
    data_.wait([this, &callback, &active_work_thread](Data& data) {
      if (active_work_thread) {
        CHECK_GE(data.active_work, 0ul);
        data.active_work--;
        active_work_thread = false;
      }

      if (data.shutting_down) return true;
      if (data.work.empty()) return false;
      callback = std::move(data.work.front());
      data.work.pop_front();
      data.active_work++;
      active_work_thread = true;
      CHECK_LE(data.active_work, size_);
      return true;
    });
    if (callback == nullptr) {
      VLOG(4) << "BackgroundThread exits.";
      return;
    }
    VLOG(9) << "BackgroundThread executing work.";
    callback();
  }
}

ThreadPoolWithWorkQueue::ThreadPoolWithWorkQueue(
    NonNull<std::shared_ptr<ThreadPool>> thread_pool,
    NonNull<std::shared_ptr<WorkQueue>> work_queue)
    : thread_pool_(std::move(thread_pool)),
      work_queue_(std::move(work_queue)) {}

const language::NonNull<std::shared_ptr<ThreadPool>>&
ThreadPoolWithWorkQueue::thread_pool() const {
  return thread_pool_;
}

const language::NonNull<std::shared_ptr<WorkQueue>>&
ThreadPoolWithWorkQueue::work_queue() const {
  return work_queue_;
}

void ThreadPoolWithWorkQueue::WaitForProgress() {
  while (work_queue()->NextExecution().has_value() ||
         thread_pool()->pending_work_units() > 0) {
    if (auto when = work_queue()->NextExecution(); when.has_value()) {
      LOG(INFO) << "Executing from work_queue: "
                << infrastructure::HumanReadableTime(when.value());
      work_queue()->Execute([when] { return when.value(); });
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  LOG(INFO) << "ThreadPoolWithWorkQueue::WaitForProgress: Done.";
}

}  // namespace afc::concurrent
