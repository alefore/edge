#include "src/concurrent/thread_pool.h"

#include "src/infrastructure/time_human.h"
#include "src/language/safe_types.h"
#include "src/tests/tests.h"

using afc::language::NonNull;
using afc::language::lazy_string::LazyString;

namespace afc::concurrent {
ThreadPool::ThreadPool(LazyString name, size_t size)
    : name_(std::move(name)), size_(size) {
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

ThreadPool::~ThreadPool() {
  LOG(INFO) << name_ << ": Starting destruction of ThreadPool.";
  std::vector<std::thread> threads;
  data_.lock([&threads](Data& data, std::condition_variable& condition) {
    CHECK(!data.shutting_down);
    data.shutting_down = true;
    condition.notify_all();
    threads.swap(data.threads);
  });
  LOG(INFO) << name_ << ": Joining threads.";
  for (auto& t : threads) t.join();
  LOG(INFO) << name_ << ": All threads are joined.";
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
    VLOG(8) << name_ << ": BackgroundThread waits for work.";
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
      VLOG(4) << name_ << ": BackgroundThread exits.";
      return;
    }
    VLOG(9) << name_ << ": BackgroundThread executing work.";
    callback();
  }
}

ThreadPoolWithWorkQueue::ThreadPoolWithWorkQueue(
    NonNull<std::shared_ptr<ThreadPool>> thread_pool,
    NonNull<std::shared_ptr<WorkQueue>> work_queue)
    : thread_pool_(std::move(thread_pool)),
      work_queue_(std::move(work_queue)) {}

ThreadPoolWithWorkQueue::~ThreadPoolWithWorkQueue() {
  LOG(INFO) << "Deletion of ThreadPoolWithWorkQueue starts.";
  work_queue_->StartShutdown();
  while (thread_pool_->pending_work_units() > 0 ||
         work_queue_->NextExecution().has_value()) {
    while (work_queue_->NextExecution().has_value()) work_queue_->Execute();
    if (thread_pool_->pending_work_units() > 0)
      // Ideally, we'd block until either the thread pool is empty or the
      // work_queue has work scheduled. Doing this is ... tricky (we'd likely
      // need to add a condition variable and receive notifications).
      //
      // So, instead, just ... sleep, ugh.
      std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
  LOG(INFO) << "Deletion of ThreadPoolWithWorkQueue proceeds.";
}

const language::NonNull<std::shared_ptr<ThreadPool>>&
ThreadPoolWithWorkQueue::thread_pool() const {
  return thread_pool_;
}

const language::NonNull<std::shared_ptr<WorkQueue>>&
ThreadPoolWithWorkQueue::work_queue() const {
  return work_queue_;
}

}  // namespace afc::concurrent
