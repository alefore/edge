// A thread-safe class that needs to depend on fields that aren't thread-safe
// can achieve this by storing those fields inside a `Data` structure and
// holding a `Protected<Data>` field. The contents of a `Protected<Data>` field
// should only be accessed by calling the `Protected<Data>::lock` method. As
// long as the class abides by a few simple expectations, the type system will
// ensures that access to these fields is serialized.
//
// This is an alternative to using a mutex explicitly.
//
// One small advantage of this approach is that if the mutex isn't needed
// (perhaps because the critical section was modified to no longer need access
// to the `Data` fields), the type system will detect this (through "variable
// is not referenced" warnings).
//
// Example:
//
// class ThreadSafeAverageComputer {
//  public:
//   void Add(int value) {
//     data_.lock([value](Data& data) {
//                  data.count ++;
//                  data.sum += value;
//                });
//   }
//
//   int Average() {
//     return data_.lock([] (const Data& data) {
//                         return data.sum / data.count;
//                       });
//   }
//
//  private:
//   struct Data {
//     int count = 0;
//     int sum = 0;
//   };
//   Protected<Data> data_ = Protected<Data>(Data{});;
// };
//

#ifndef __AFC_EDITOR_PROTECTED_H__
#define __AFC_EDITOR_PROTECTED_H__

#include <chrono>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>

#include "src/tests/concurrent_interfaces.h"

namespace afc::concurrent {
template <typename T>
struct EmptyValidator {
  void operator()(const T&) const {}
};

template <typename T, typename Validator = EmptyValidator<T>,
          bool test_flows_register = true>
class Protected {
 public:
  using Lock = std::unique_ptr<T, std::function<void(T*)>>;
  using ConstLock = std::unique_ptr<const T, std::function<void(const T*)>>;

  Protected(T t, Validator validator = Validator{})
      : data_(std::move(t)), validator_(std::move(validator)) {
    validator_(data_);  // No need to lock: we know we're the only owners.
  }

  Protected() = default;
  Protected(Protected&&) = default;
  Protected& operator=(Protected&&) = default;

  ~Protected() {
    if (mutex_ != nullptr) {
      MaybeRegisterLock();
      std::unique_lock<std::mutex> lock(*mutex_);
      MaybeRegisterUnlock();
      validator_(data_);
    }
  }

  Lock lock() {
    MaybeRegisterLock();
    mutex_->lock();
    return Lock(&data_, [this](T*) {
      validator_(data_);
      mutex_->unlock();
      MaybeRegisterUnlock();
    });
  }

  ConstLock lock() const {
    MaybeRegisterLock();
    mutex_->lock();
    return ConstLock(&data_, [this](const T*) {
      // No need to validate; we only gave constant access.
      mutex_->unlock();
      MaybeRegisterUnlock();
    });
  }

  template <typename Callable>
  auto lock(Callable callable) {
    return callable(*lock());
  }

  template <typename Callable>
  auto lock(Callable callable) const {
    return callable(*lock());
  }

 protected:
  void MaybeRegisterLock() const {
    if constexpr (test_flows_register)
      if (tests::concurrent::GetGlobalHandler() != nullptr)
        tests::concurrent::GetGlobalHandler()->Lock(*mutex_);
  }

  void MaybeRegisterUnlock() const {
    if constexpr (test_flows_register)
      if (tests::concurrent::GetGlobalHandler() != nullptr)
        tests::concurrent::GetGlobalHandler()->Unlock(*mutex_);
  }

  mutable std::unique_ptr<std::mutex> mutex_ = std::make_unique<std::mutex>();
  T data_;
  Validator validator_ = Validator{};
};

template <typename T, typename Validator = EmptyValidator<T>,
          bool test_flows_register = true>
class ProtectedWithCondition
    : public Protected<T, Validator, test_flows_register> {
 public:
  ProtectedWithCondition(T t, Validator validator = Validator{})
      : Protected<T, Validator, test_flows_register>(std::move(t),
                                                     std::move(validator)) {}

  template <typename Callable>
  auto lock(Callable callable) {
    return Protected<T, Validator, test_flows_register>::lock(
        [&](T& t) { return callable(t, condition_); });
  }

  template <typename Callable>
  auto lock(Callable callable) const {
    return Protected<T, Validator, test_flows_register>::lock(
        [&](const T& t) { return callable(t, condition_); });
  }

  template <typename Callable>
  void wait(Callable callable) {
    std::unique_lock<std::mutex> mutex_lock(
        *Protected<T, Validator, test_flows_register>::mutex_);
    condition_.wait(mutex_lock, [this, &callable] {
      return callable(Protected<T, Validator, test_flows_register>::data_);
    });
    Protected<T, Validator, test_flows_register>::validator_(
        Protected<T, Validator, test_flows_register>::data_);
  }

  template <typename Callable>
  void wait(Callable callable) const {
    std::unique_lock<std::mutex> mutex_lock(
        *Protected<T, Validator, test_flows_register>::mutex_);
    condition_.wait(mutex_lock, [this, &callable] {
      return callable(Protected<T, Validator, test_flows_register>::data_);
    });
  }

  template <typename Callable, typename Clock, typename Duration>
  bool wait_until(const std::chrono::time_point<Clock, Duration>& timeout,
                  Callable callable) {
    std::unique_lock<std::mutex> mutex_lock(
        *Protected<T, Validator, test_flows_register>::mutex_);
    return condition_.wait_until(mutex_lock, timeout, [this, &callable] {
      return callable(Protected<T, Validator, test_flows_register>::data_);
    });
  }

 private:
  mutable std::condition_variable condition_;
};
}  // namespace afc::concurrent
#endif  //__AFC_EDITOR_PROTECTED_H__
