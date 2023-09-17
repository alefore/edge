#ifndef __AFC_EDITOR_CONCURRENT_OPERATION_H__
#define __AFC_EDITOR_CONCURRENT_OPERATION_H__

#include <glog/logging.h>

#include "src/concurrent/protected.h"
#include "src/concurrent/thread_pool.h"
#include "src/language/safe_types.h"

namespace afc::concurrent {
// A barrier: blocks deletion until all callables given to `Operation::Add` have
// successfully executed.
class Operation {
 public:
  Operation(
      ThreadPool& thread_pool,
      std::optional<size_t> concurrency_limit = std::nullopt,
      std::unique_ptr<bool, std::function<void(bool*)>> tracker_call = nullptr)
      : thread_pool_(thread_pool),
        tracker_call_(std::move(tracker_call)),
        concurrency_limit_(concurrency_limit) {}

  ~Operation() {
    VLOG(5) << "Operation destruction.";
    BlockUntilDone();
  }

  template <typename Callable>
  void Add(Callable callable) const {
    LockSlot();
    thread_pool_.RunIgnoringResult([this, callable = std::make_shared<Callable>(
                                              std::move(callable))]() mutable {
      VLOG(8) << "Running callable.";
      (*callable)();
      // Allow it to release its dependencies before we decrement our counter,
      // in case deletion needs synchronization.
      callable = nullptr;
      VLOG(9) << "Callable returned.";
      pending_operations_.lock(
          [this](unsigned int& i, std::condition_variable& c) {
            CHECK_GT(i, 0ul);
            if (concurrency_limit_.has_value())
              CHECK_LE(i, concurrency_limit_.value());
            i--;
            VLOG(7) << "Decremented operations: " << i;
            if (i == 0) c.notify_one();
          });
    });
  }

  void BlockUntilDone() const {
    pending_operations_.wait([](unsigned int& i) {
      VLOG(6) << "Checking operation with: " << i;
      return i == 0;
    });
    VLOG(4) << "Operation done.";
  }

 private:
  void LockSlot() const {
    language::VisitOptional(
        [this](size_t limit) {
          pending_operations_.wait([limit](unsigned int& i) {
            CHECK_LE(i, limit);
            VLOG(6) << "Checking operation with: " << i;
            if (i == limit) return false;
            i++;
            return true;
          });
        },
        [this] {
          pending_operations_.lock(
              [](unsigned int& i, std::condition_variable&) {
                i++;
                VLOG(7) << "Increment operations: " << i;
              });
        },
        concurrency_limit_);
  }

  ThreadPool& thread_pool_;
  const std::unique_ptr<bool, std::function<void(bool*)>> tracker_call_;
  const std::optional<size_t> concurrency_limit_;
  mutable ProtectedWithCondition<unsigned int> pending_operations_ =
      ProtectedWithCondition<unsigned int>(0);
};

}  // namespace afc::concurrent

#endif
