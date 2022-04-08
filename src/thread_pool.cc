#include "src/thread_pool.h"

#include <cctype>
#include <ostream>

#include "src/notification.h"
#include "src/tests/tests.h"

namespace afc::editor {
ThreadPool::ThreadPool(size_t size,
                       std::shared_ptr<WorkQueue> completion_work_queue)
    : completion_work_queue_(completion_work_queue), data_(Data{.size = size}) {
  data_.lock([this](Data& data, std::condition_variable&) {
    for (size_t i = 0; i < data.size; i++) {
      data.threads.push_back(std::thread([this]() { BackgroundThread(); }));
    }
  });
}

ThreadPool::~ThreadPool() {
  std::vector<std::thread> threads;
  data_.lock([&threads](Data& data, std::condition_variable& condition) {
    CHECK(!data.shutting_down);
    data.shutting_down = true;
    condition.notify_all();
    threads.swap(data.threads);
  });
  LOG(INFO) << "Joining threads.";
  for (auto& t : threads) t.join();
}

void ThreadPool::Schedule(std::function<void()> work) {
  CHECK(work != nullptr);
  data_.lock([&work](Data& data, std::condition_variable& condition) {
    CHECK(!data.shutting_down);
    data.work.push_back(std::move(work));
    condition.notify_one();
  });
}

void ThreadPool::BackgroundThread() {
  while (true) {
    std::function<void()> callback;
    data_.wait([&callback](Data& data) {
      CHECK(callback == nullptr);
      if (data.shutting_down) return true;
      if (data.work.empty()) return false;
      callback = std::move(data.work.front());
      data.work.pop_front();
      return true;
    });
    if (callback == nullptr) return;
    callback();
  }
}

}  // namespace afc::editor
