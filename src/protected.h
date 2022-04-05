#ifndef __AFC_EDITOR_PROTECTED_H__
#define __AFC_EDITOR_PROTECTED_H__

namespace afc::editor {
template <typename T>
class Protected {
 public:
  using Lock = std::unique_ptr<T, std::function<void(T*)>>;

  Protected(T t) : data_(std::move(t)) {}
  Protected() = default;

  ~Protected() { std::unique_lock<std::mutex> lock(mutex_); }

  Lock lock() {
    mutex_.lock();
    return Lock(&data_, [this](T*) { mutex_.unlock(); });
  }

  template <typename Callable>
  auto lock(Callable callable) {
    std::unique_lock<std::mutex> lock(mutex_);
    return callable(data_);
  }

 private:
  std::mutex mutex_;
  T data_;
};
}  // namespace afc::editor
#endif  //__AFC_EDITOR_PROTECTED_H__
