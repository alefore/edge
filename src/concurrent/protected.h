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

#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>

namespace afc::concurrent {
template <typename T>
struct EmptyValidator {
  void operator()(const T&) const {}
};

template <typename T, typename Validator = EmptyValidator<T>>
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
      std::unique_lock<std::mutex> lock(*mutex_);
      validator_(data_);
    }
  }

  Lock lock() {
    mutex_->lock();
    return Lock(&data_, [this](T*) {
      validator_(data_);
      mutex_->unlock();
    });
  }

  ConstLock lock() const {
    mutex_->lock();
    return ConstLock(&data_, [this](const T*) {
      // No need to validate; we only gave constant access.
      mutex_->unlock();
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
  mutable std::unique_ptr<std::mutex> mutex_ = std::make_unique<std::mutex>();
  T data_;
  Validator validator_ = Validator{};
};

template <typename T, typename Validator = EmptyValidator<T>>
class ProtectedWithCondition : public Protected<T, Validator> {
 public:
  ProtectedWithCondition(T t, Validator validator = Validator{})
      : Protected<T, Validator>(std::move(t), std::move(validator)) {}

  template <typename Callable>
  auto lock(Callable callable) {
    return Protected<T>::lock([&](T& t) { return callable(t, condition_); });
  }

  template <typename Callable>
  auto lock(Callable callable) const {
    return Protected<T>::lock(
        [&](const T& t) { return callable(t, condition_); });
  }

  template <typename Callable>
  void wait(Callable callable) {
    std::unique_lock<std::mutex> mutex_lock(*Protected<T, Validator>::mutex_);
    condition_.wait(mutex_lock, [this, &callable] {
      return callable(Protected<T, Validator>::data_);
    });
    Protected<T, Validator>::validator_(Protected<T, Validator>::data_);
  }

  template <typename Callable>
  void wait(Callable callable) const {
    std::unique_lock<std::mutex> mutex_lock(*Protected<T, Validator>::mutex_);
    condition_.wait(mutex_lock, [this, &callable] {
      return callable(Protected<T, Validator>::data_);
    });
  }

 private:
  mutable std::condition_variable condition_;
};
}  // namespace afc::concurrent
#endif  //__AFC_EDITOR_PROTECTED_H__
