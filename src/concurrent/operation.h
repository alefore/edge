#ifndef __AFC_EDITOR_CONCURRENT_OPERATION_H__
#define __AFC_EDITOR_CONCURRENT_OPERATION_H__

#include <glog/logging.h>

#include "src/concurrent/protected.h"
#include "src/concurrent/thread_pool.h"

namespace afc::concurrent {
// A barrier: blocks deletion until all callables given to `Operation::Add` have
// successfully executed.
class Operation {
 public:
  Operation(ThreadPool& thread_pool) : thread_pool_(thread_pool) {}

  ~Operation() {
    VLOG(5) << "Operation destruction.";
    pending_operations_.wait([](unsigned int& i) {
      VLOG(6) << "Checking operation with: " << i;
      return i == 0;
    });
    VLOG(4) << "Operation done.";
  }

  template <typename Callable>
  void Add(Callable callable) {
    pending_operations_.lock([](unsigned int& i, std::condition_variable&) {
      i++;
      VLOG(7) << "Increment operations: " << i;
    });
    thread_pool_.RunIgnoringResult([this, callable = std::make_shared<Callable>(
                                              std::move(callable))] {
      VLOG(8) << "Running callable.";
      (*callable)();
      VLOG(9) << "Callable returned.";
      pending_operations_.lock([](unsigned int& i, std::condition_variable& c) {
        CHECK_GT(i, 0ul);
        i--;
        VLOG(7) << "Decremented operations: " << i;
        if (i == 0) c.notify_one();
      });
    });
  }

 private:
  ThreadPool& thread_pool_;
  ProtectedWithCondition<unsigned int> pending_operations_ =
      ProtectedWithCondition<unsigned int>(0);
};
}  // namespace afc::concurrent

#endif
