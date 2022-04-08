#ifndef __AFC_EDITOR_THREAD_POOL_H__
#define __AFC_EDITOR_THREAD_POOL_H__

#include <thread>

#include "src/futures/futures.h"
#include "src/work_queue.h"

namespace afc::editor {
class ThreadPool {
 public:
  ThreadPool(size_t size, std::shared_ptr<WorkQueue> completion_work_queue);
  ~ThreadPool();

  // Evaluates a producer in a background thread and returns a future that will
  // receive the value. The future will be notified through
  // completion_work_queue, which can be used to ensure that only certain
  // threads receive the produced values.
  template <typename Callable>
  auto Run(Callable producer) {
    futures::Future<decltype(producer())> output;
    Schedule([producer = std::move(producer), consumer = output.consumer,
              work_queue = completion_work_queue_] {
      work_queue->Schedule([consumer, value = producer()] { consumer(value); });
    });
    return std::move(output.value);
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