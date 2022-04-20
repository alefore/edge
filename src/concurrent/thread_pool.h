#ifndef __AFC_EDITOR_THREAD_POOL_H__
#define __AFC_EDITOR_THREAD_POOL_H__

#include <functional>
#include <list>
#include <memory>
#include <thread>
#include <vector>

#include "src/concurrent/work_queue.h"
#include "src/futures/futures.h"

namespace afc::editor {
class ThreadPool {
 public:
  ThreadPool(size_t size, std::shared_ptr<WorkQueue> completion_work_queue);
  ~ThreadPool();

  // Evaluates a producer in a background thread and returns a future that will
  // receive the value. The future will be notified through
  // completion_work_queue, which can be used to ensure that only certain
  // threads receive the produced values.
  //
  // Should only be called if completion_work_queue is non-nullptr.
  template <typename Callable>
  auto Run(Callable callable) {
    futures::Future<decltype(callable())> output;
    // We copy callable into a shared pointer in case it's not copyable.
    auto shared_callable = std::make_shared<Callable>(std::move(callable));
    Schedule([shared_callable = std::move(shared_callable),
              consumer = output.consumer, work_queue = completion_work_queue_] {
      CHECK(work_queue != nullptr);
      work_queue->Schedule(
          [consumer, value = (*shared_callable)()] { consumer(value); });
    });
    return std::move(output.value);
  }

  template <typename Callable>
  void RunIgnoringResult(Callable callable) {
    Schedule([callable = std::move(callable)] { callable(); });
  }

 private:
  void Schedule(std::function<void()> work);
  void BackgroundThread();

  const std::shared_ptr<WorkQueue> completion_work_queue_;
  struct Data {
    const size_t size;
    bool shutting_down = false;
    std::vector<std::thread> threads = {};
    std::list<std::function<void()>> work = {};
  };
  ProtectedWithCondition<Data> data_;
};
}  // namespace afc::editor
#endif  //__AFC_EDITOR_THREAD_POOL_H__