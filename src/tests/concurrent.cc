#include "src/tests/concurrent.h"

#include <ranges>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "src/concurrent/protected.h"
#include "src/futures/delete_notification.h"
#include "src/language/container.h"
#include "src/language/ghost_type.h"
#include "src/language/hash.h"
#include "src/tests/concurrent_interfaces.h"

using afc::language::compute_hash;
using afc::language::EraseOrDie;
using afc::language::GetValueOrDie;
using afc::language::hash_combine;
using afc::language::InsertOrDie;
using afc::language::MakeHashableIteratorRange;
using afc::language::MakeNonNullShared;
using afc::language::NonNull;
using ::operator<<;

namespace afc::tests::concurrent {
namespace {
GHOST_TYPE_SIZE_T(OperationId);

struct LockId {
  OperationId operation;
  size_t lock;
};

struct Breakpoint {
  OperationId operation;
  LockId lock;
};

bool operator==(const LockId& a, const LockId& b) {
  return a.operation == b.operation && a.lock == b.lock;
}

bool operator==(const Breakpoint& a, const Breakpoint& b) {
  return a.operation == b.operation && a.lock == b.lock;
}

GHOST_TYPE_CONTAINER(Trace, std::vector<Breakpoint>);

#if 0
std::ostream& operator<<(std::ostream& os, const LockId& obj) {
  os << obj.operation << ":" << obj.lock;
  return os;
}

std::ostream& operator<<(std::ostream& os, const Breakpoint& obj) {
  os << "[" << obj.operation << ", " << obj.lock << "]";
  return os;
}

std::ostream& operator<<(std::ostream& os, const Trace& obj) {
  for (const Breakpoint& b : obj) os << b;
  return os;
}
#endif
}  // namespace
}  // namespace afc::tests::concurrent
namespace std {
template <>
struct hash<afc::tests::concurrent::LockId> {
  std::size_t operator()(const afc::tests::concurrent::LockId& bp) const {
    return compute_hash(bp.operation, bp.lock);
  }
};

template <>
struct hash<typename afc::tests::concurrent::Breakpoint> {
  std::size_t operator()(
      const typename afc::tests::concurrent::Breakpoint& bp) const {
    return compute_hash(bp.operation, bp.lock);
  }
};

template <>
struct hash<typename std::vector<typename afc::tests::concurrent::Breakpoint>> {
  std::size_t operator()(
      const std::vector<typename afc::tests::concurrent::Breakpoint>& trace)
      const {
    size_t output = 39487;
    for (const auto& bp : trace)
      output = hash_combine(output, compute_hash(bp));
    return output;
  }
};

}  // namespace std
GHOST_TYPE_TOP_LEVEL(afc::tests::concurrent::OperationId);
GHOST_TYPE_TOP_LEVEL(afc::tests::concurrent::Trace);
namespace afc::tests::concurrent {
namespace {
class Notification {
 public:
  void Wait() {
    value_.wait([](bool& value) { return value; });
  }

  void Notify() {
    value_.lock([](bool& value, std::condition_variable& c) {
      CHECK(!value);
      value = true;
      c.notify_all();
    });
  }

 private:
  afc::concurrent::ProtectedWithCondition<
      bool, afc::concurrent::EmptyValidator<bool>, false>
      value_ = false;
};

// Holds the state of a single execution.
//
// Not thread-safe. Customer must synchronize.
class Execution {
 public:
  OperationId ReserveOperationId() {
    ++next_operation_;
    LOG(INFO) << "Reserving ID: " << next_operation_;
    InsertOrDie(expected_operations_, next_operation_);
    return next_operation_;
  }

  LockId LookUp(const std::mutex& mutex) {
    if (auto it = lock_map_.find(&mutex); it != lock_map_.end())
      return it->second;

    OperationId op = CurrentOperation();
    return InsertOrDie(lock_map_,
                       {&mutex, {.operation = op, .lock = next_lock_[op]++}})
        ->second;
  }

  void AddThread(OperationId operation_id) {
    LOG(INFO) << "AddThread: " << std::this_thread::get_id();
    EraseOrDie(expected_operations_, operation_id);
    InsertOrDie(threads_, {std::this_thread::get_id(), operation_id});
  }

  void RemoveThread() {
    LOG(INFO) << "RemoveThread: " << std::this_thread::get_id();
    EraseOrDie(threads_, std::this_thread::get_id());
  }

  void MarkLocked(LockId lock) { InsertOrDie(locked_locks_, lock); }

  void MarkUnlocked(LockId lock) { EraseOrDie(locked_locks_, lock); }

  bool ThreadsRunning() {
    return GetBreakpoints().size() <
           expected_operations_.size() + threads_.size();
  }

  void AddLockIntent(const std::mutex& mutex,
                     NonNull<Notification*> notification) {
    InsertOrDie(waiting_threads_[LookUp(mutex)],
                {CurrentOperation(), notification});
  }

  void RegisterUnlock(const std::mutex& mutex) {
    EraseOrDie(locked_locks_, LookUp(mutex));
  }

  OperationId CurrentOperation() const {
    return GetValueOrDie(threads_, std::this_thread::get_id());
  }

  std::unordered_set<Breakpoint> GetBreakpoints() const {
    std::unordered_set<Breakpoint> output;
    for (const std::pair<
             const LockId,
             std::unordered_map<OperationId, NonNull<Notification*>>>& entry :
         waiting_threads_)
      for (const OperationId& operation : entry.second | std::views::keys)
        InsertOrDie(output,
                    Breakpoint{.operation = operation, .lock = entry.first});
    return output;
  }

  std::unordered_set<Breakpoint> GetEligibleBreakpoints() const {
    std::unordered_set<Breakpoint> output = GetBreakpoints();
    std::erase_if(output, [&](const Breakpoint& b) {
      return locked_locks_.contains(b.lock);
    });
    return output;
  }

  NonNull<Notification*> PrepareToAdvance(Breakpoint breakpoint) {
    InsertOrDie(locked_locks_, breakpoint.lock);
    return PopValueOrDie(GetValueOrDie(waiting_threads_, breakpoint.lock),
                         breakpoint.operation);
  }

 private:
  OperationId next_operation_;
  std::unordered_set<OperationId> expected_operations_;
  std::unordered_map<OperationId, size_t> next_lock_;
  std::unordered_map<std::thread::id, OperationId> threads_;
  std::unordered_map<const std::mutex*, LockId> lock_map_;
  std::unordered_set<LockId> locked_locks_;
  std::unordered_map<LockId,
                     std::unordered_map<OperationId, NonNull<Notification*>>>
      waiting_threads_;
};

class HandlerImpl : public Handler {
 public:
  HandlerImpl(Options options) : options_(std::move(options)) {}

  void Run() {
    LOG(INFO) << "Setting global handler.";
    SetGlobalHandler(this);
    unexplored_traces_ = {Trace()};
    size_t runs = 0;
    while (!unexplored_traces_.empty()) {
      execution_ = std::make_unique<afc::concurrent::ProtectedWithCondition<
          Execution, afc::concurrent::EmptyValidator<Execution>, false>>(
          Execution{});

      LOG(INFO) << "Starting run: " << runs++
                << " (unexplored: " << unexplored_traces_.size() << ")";

      options_.thread_pool->RunIgnoringResult(options_.start);

      LOG(INFO) << "Set up.";
      WaitForThreads();

      LOG(INFO) << "Restoring state.";
      for (const Breakpoint& breakpoint : unexplored_traces_.back())
        ExpandBreakpoint(breakpoint);

      unexplored_traces_.pop_back();

      LOG(INFO) << "Exploring new states.";
      while (PushNewTraces()) {
        // Pick the last discovered neighbor and descend into it.
        ExpandBreakpoint(unexplored_traces_.back().back());
        unexplored_traces_.pop_back();
      }
      trace_.clear();
      execution_ = nullptr;
    }
    LOG(INFO) << "Resetting global handler.";
    SetGlobalHandler(nullptr);
  }

  void Lock(const std::mutex& mutex) override {
    Notification notification;
    execution_->lock(
        [&](Execution& threads_map, std::condition_variable& condition) {
          threads_map.AddLockIntent(
              mutex, NonNull<Notification*>::AddressOf(notification));
          condition.notify_one();
        });
    notification.Wait();
  }

  void Unlock(const std::mutex& mutex) override {
    execution_->lock([&](Execution& threads_map, std::condition_variable&) {
      threads_map.RegisterUnlock(mutex);
    });
  }

  std::function<void()> Wrap(std::function<void()> work) override {
    OperationId operation_id =
        execution_->lock([](Execution& threads_map, auto&) {
          return threads_map.ReserveOperationId();
        });
    return [this, operation_id, work = std::move(work)]() mutable {
      execution_->lock(
          [operation_id](Execution& m, std::condition_variable& condition) {
            m.AddThread(operation_id);
            condition.notify_one();
          });
      work();
      // We drop `work` explicitly before we remove the thread from the
      // registry, in case deletion of objects captured in lambda form reaches
      // breakpoints.
      work = nullptr;
      execution_->lock([](Execution& m, std::condition_variable& condition) {
        m.RemoveThread();
        condition.notify_one();
      });
    };
  }

 private:
  bool PushNewTraces() {
    return execution_->lock([&](const Execution& data, auto&) {
      // Push all newly discovered neighbors into `unexplored_traces_`.
      std::unordered_set<Breakpoint> breakpoints =
          data.GetEligibleBreakpoints();
      for (const Breakpoint& breakpoint : breakpoints) {
        unexplored_traces_.push_back(trace_);
        unexplored_traces_.back().push_back(breakpoint);
      }
      return !breakpoints.empty();
    });
  }

  void ExpandBreakpoint(Breakpoint breakpoint) {
    trace_.push_back(breakpoint);
    execution_
        ->lock([breakpoint](Execution& breakpoints, auto&) {
          return breakpoints.PrepareToAdvance(breakpoint);
        })
        ->Notify();
    WaitForThreads();
  }

  void WaitForThreads() {
    // TODO(trivial, 2023-09-25): Figure out how to pass the right timeout.
    if (auto it = traces_map_.find(trace_); it != traces_map_.end()) {
      CHECK(execution_->wait_until(
          std::chrono::system_clock::now() + std::chrono::milliseconds(500),
          [&](Execution& threads_map) {
            return threads_map.GetEligibleBreakpoints() == it->second;
          }));
    } else {
      execution_->wait_until(
          std::chrono::system_clock::now() + std::chrono::milliseconds(500),
          [&](Execution& threads_map) {
            return !threads_map.ThreadsRunning();
          });
      InsertOrDie(traces_map_,
                  {trace_, execution_->lock([](Execution& data, auto&) {
                     return data.GetEligibleBreakpoints();
                   })});
    }
  }

  const Options options_;

  Trace trace_ = Trace(std::vector<Breakpoint>{});

  std::unique_ptr<afc::concurrent::ProtectedWithCondition<
      Execution, afc::concurrent::EmptyValidator<Execution>, false>>
      execution_ = nullptr;

  std::vector<Trace> unexplored_traces_ = {Trace({})};

  // Register sets of breakpoints known to be reached after executing a trace;
  // Mostly as an optimization (to reduce the wait time when re-running
  // traces).
  std::unordered_map<Trace, std::unordered_set<Breakpoint>> traces_map_ =
      std::unordered_map<Trace, std::unordered_set<Breakpoint>>();
};

}  // namespace

void TestFlows(Options options) { HandlerImpl(std::move(options)).Run(); }

}  // namespace afc::tests::concurrent
