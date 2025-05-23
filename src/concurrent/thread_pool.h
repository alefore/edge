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
// Prefer using concurrent::OperationFactory over scheduling directly to the
// thread pool.
class ThreadPool {
 public:
  ThreadPool(size_t size);
  ~ThreadPool();

  size_t size() const;

  template <typename Callable>
  void RunIgnoringResult(Callable callable) {
    // We copy callable into a shared pointer in case it's not copyable.
    Schedule([shared_callable = std::make_shared<Callable>(
                  std::move(callable))] { (*shared_callable)(); });
  }

 private:
  void Schedule(std::function<void()> work);
  void BackgroundThread();

  const size_t size_;
  struct Data {
    bool shutting_down = false;
    std::vector<std::thread> threads = {};
    std::list<std::function<void()>> work = {};
  };
  ProtectedWithCondition<Data, EmptyValidator<Data>, false> data_ =
      ProtectedWithCondition<Data, EmptyValidator<Data>, false>(Data{});
};

// This is very similar to ThreadPool, but holds a work_queue. This allows us to
// define a futures-based `Run` method: we return a future that will be notied
// when the callable finishes inside the work_queue.
class ThreadPoolWithWorkQueue {
 public:
  ThreadPoolWithWorkQueue(
      language::NonNull<std::shared_ptr<ThreadPool>> thread_pool,
      language::NonNull<std::shared_ptr<WorkQueue>> work_queue);

  const language::NonNull<std::shared_ptr<ThreadPool>>& thread_pool() const;
  const language::NonNull<std::shared_ptr<WorkQueue>>& work_queue() const;

  template <typename Callable>
  void RunIgnoringResult(Callable callable) {
    thread_pool()->RunIgnoringResult(std::move(callable));
  }

  // Evaluates a producer in a background thread and returns a future that will
  // receive the value. The future will be notified through
  // completion_work_queue, which can be used to ensure that only certain
  // threads receive the produced values.
  template <typename Callable>
  auto Run(Callable callable) {
    futures::Future<decltype(callable())> output;
    thread_pool()->RunIgnoringResult([callable,
                                      consumer = std::move(output.consumer),
                                      work_queue = work_queue_] mutable {
      work_queue->Schedule(
          WorkQueue::Callback{.callback = [consumer = std::move(consumer),
                                           value = callable()] mutable {
            std::invoke(std::move(consumer), std::move(value));
          }});
    });
    return std::move(output.value);
  }

 private:
  const language::NonNull<std::shared_ptr<ThreadPool>> thread_pool_;
  const language::NonNull<std::shared_ptr<WorkQueue>> work_queue_;
};

}  // namespace afc::concurrent
#endif  //__AFC_EDITOR_THREAD_POOL_H__