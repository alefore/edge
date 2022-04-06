#ifndef __AFC_EDITOR_PROTECTED_H__
#define __AFC_EDITOR_PROTECTED_H__

namespace afc::editor {
template <typename T>
class Protected {
 public:
  using Lock = std::unique_ptr<T, std::function<void(T*)>>;
  using ConstLock = std::unique_ptr<const T, std::function<void(const T*)>>;

  Protected(T t) : data_(std::move(t)) {}
  Protected() = default;

  ~Protected() { std::unique_lock<std::mutex> lock(mutex_); }

  Lock lock() {
    mutex_.lock();
    return Lock(&data_, [this](T*) { mutex_.unlock(); });
  }

  ConstLock lock() const {
    mutex_.lock();
    return ConstLock(&data_, [this](const T*) { mutex_.unlock(); });
  }

  template <typename Callable>
  auto lock(Callable callable) {
    std::unique_lock<std::mutex> lock(mutex_);
    return callable(data_);
  }

  template <typename Callable>
  auto lock(Callable callable) const {
    std::unique_lock<std::mutex> lock(mutex_);
    return callable(data_);
  }

 private:
  mutable std::mutex mutex_;
  T data_;
};
}  // namespace afc::editor
#endif  //__AFC_EDITOR_PROTECTED_H__
