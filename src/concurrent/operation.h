#ifndef __AFC_EDITOR_CONCURRENT_OPERATION_H__
#define __AFC_EDITOR_CONCURRENT_OPERATION_H__

#include <glog/logging.h>

#include "src/concurrent/protected.h"
#include "src/concurrent/thread_pool.h"

namespace afc::concurrent {
class Operation {
 public:
  Operation(ThreadPool& thread_pool) : thread_pool_(thread_pool) {}

  ~Operation() {
    VLOG(5) << "Operation destruction.";
    pending_operations_.wait([](int& i) {
      VLOG(6) << "Checking operation with: " << i;
      return i == 0;
    });
    VLOG(4) << "Operation done.";
  }

  template <typename Callable>
  void Add(Callable callable) {
    pending_operations_.lock([](int& i, std::condition_variable&) {
      i++;
      VLOG(7) << "Increment operations: " << i;
    });
    thread_pool_.RunIgnoringResult(
        [this, callable = std::make_shared<Callable>(std::move(callable))] {
          VLOG(8) << "Running callable.";
          (*callable)();
          VLOG(9) << "Callable returned.";
          pending_operations_.lock([](int& i, std::condition_variable& c) {
            i--;
            VLOG(7) << "Decremented operations: " << i;
            c.notify_one();
          });
        });
  }

 private:
  ThreadPool& thread_pool_;
  ProtectedWithCondition<int> pending_operations_ =
      ProtectedWithCondition<int>(0);
};
}  // namespace afc::concurrent

#endif
