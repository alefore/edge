#ifndef __AFC_EDITOR_THREAD_POOL_H__
#define __AFC_EDITOR_THREAD_POOL_H__

#include <functional>
#include <list>
#include <memory>
#include <thread>
#include <vector>

#include "src/concurrent/work_queue.h"
#include "src/futures/futures.h"

namespace afc::concurrent {
class ThreadPool {
 public:
  // completion_work_queue can be null (in which case `Run` should not be
  // called).
  //
  // TODO(2023-07-04): Find a way to separate this into two types, perhaps
  // a subtype.
  ThreadPool(size_t size, std::shared_ptr<WorkQueue> completion_work_queue);
  ~ThreadPool();

  size_t size() const;

  // Evaluates a producer in a background thread and returns a future that will
  // receive the value. The future will be notified through
  // completion_work_queue, which can be used to ensure that only certain
  // threads receive the produced values.
  //
  // Should only be called if completion_work_queue is non-nullptr.
  template <typename Callable>
  auto Run(Callable callable) {
    futures::Future<decltype(callable())> output;
    RunIgnoringResult([callable, consumer = output.consumer,
                       work_queue = completion_work_queue_] {
      CHECK(work_queue != nullptr);
      work_queue->Schedule(WorkQueue::Callback{
          .callback = std::bind_front(consumer, callable())});
    });
    return std::move(output.value);
  }

  template <typename Callable>
  void RunIgnoringResult(Callable callable) {
    // We copy callable into a shared pointer in case it's not copyable.
    Schedule([shared_callable = std::make_shared<Callable>(
                  std::move(callable))] { (*shared_callable)(); });
  }

 private:
  void Schedule(std::function<void()> work);
  void BackgroundThread();

  const std::shared_ptr<WorkQueue> completion_work_queue_;
  const size_t size_;
  struct Data {
    bool shutting_down = false;
    std::vector<std::thread> threads = {};
    std::list<std::function<void()>> work = {};
  };
  ProtectedWithCondition<Data> data_ = ProtectedWithCondition<Data>(Data{});
};
}  // namespace afc::concurrent
#endif  //__AFC_EDITOR_THREAD_POOL_H__