#ifndef __AFC_EDITOR_PROTECTED_H__
#define __AFC_EDITOR_PROTECTED_H__

#include <memory>
#include <mutex>

namespace afc::editor {
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

  ~Protected() {
    std::unique_lock<std::mutex> lock(mutex_);
    validator_(data_);
  }

  Lock lock() {
    mutex_.lock();
    validator_(data_);
    return Lock(&data_, [this](T*) {
      validator_(data_);
      mutex_.unlock();
    });
  }

  ConstLock lock() const {
    mutex_.lock();
    validator_(data_);
    return ConstLock(&data_, [this](const T*) {
      // No need to validate; we only gave constant access.
      mutex_.unlock();
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

 private:
  mutable std::mutex mutex_;
  T data_;
  const Validator validator_ = Validator{};
};
}  // namespace afc::editor
#endif  //__AFC_EDITOR_PROTECTED_H__
