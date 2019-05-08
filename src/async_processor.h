#ifndef __AFC_EDITOR_ASYNC_PROCESSOR_H__
#define __AFC_EDITOR_ASYNC_PROCESSOR_H__

#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>

namespace afc {
namespace editor {

// Function that runs `supplier` asynchronously (with a background thread) and
// allows the user to trigger execution and fetch the results of the last
// completed execution.
//
// This class is thread-safe.
template <typename Input, typename Output>
class AsyncProcessor {
 public:
  // A function that receives an Input instance and returns an Output instance.
  // This will be executed in the background thread (so it must be thread-safe
  // with other threads it shares data with).
  using Factory = std::function<Output(Input)>;
  // A function to run once the output of a call to Factory has been correctly
  // installed.
  using NotifyCallback = std::function<void()>;

  AsyncProcessor(Factory factory, NotifyCallback notify_callback)
      : factory_(std::move(factory)),
        notify_callback_(std::move(notify_callback)) {}

  ~AsyncProcessor() { PauseThread(); }

  void Push(Input input) {
    thread_creation_mutex_.lock();
    mutex_.lock();
    input_ = std::move(input);
    if (!background_thread_.joinable()) {
      background_thread_shutting_down_ = false;
      LOG(INFO) << "Creating thread";
      background_thread_ = std::thread([this]() { BackgroundThread(); });
      VLOG(5) << "Thread created.";
    }
    mutex_.unlock();
    thread_creation_mutex_.unlock();

    background_condition_.notify_one();
  }

  std::optional<Output> Get() const {
    std::unique_lock<std::mutex> lock(mutex_);
    return output_;
  }

  void PauseThread() {
    std::unique_lock<std::mutex> thread_creation_lock(thread_creation_mutex_);
    if (!background_thread_.joinable()) {
      return;
    }

    std::unique_lock<std::mutex> lock(mutex_);
    background_thread_shutting_down_ = true;
    lock.unlock();

    background_condition_.notify_one();
    background_thread_.join();
  }

 private:
  void BackgroundThread() {
    while (true) {
      std::optional<Input> input;

      std::unique_lock<std::mutex> lock(mutex_);
      background_condition_.wait(lock, [this]() {
        return background_thread_shutting_down_ || input_.has_value();
      });
      VLOG(5) << "Background thread is waking up.";
      if (background_thread_shutting_down_) {
        return;
      }
      CHECK(input_.has_value());
      std::swap(input, input_);

      lock.unlock();

      const Output output = factory_(std::move(input.value()));

      mutex_.lock();
      output_ = std::move(output);
      mutex_.unlock();

      notify_callback_();
    }
  }

  const Factory factory_;
  const NotifyCallback notify_callback_;

  // If set, pending input for the background thread to pick up.
  std::optional<Input> input_;
  // As soon as the first execution of the supplier completes, we store here the
  // value it returns. Whenever it finishes, we update the previous value.
  std::optional<Output> output_;

  // Protects all the variables that background thread may access.
  mutable std::mutex mutex_;
  std::condition_variable background_condition_;

  // Protects access to background_thread_ itself. Must never be acquired after
  // mutex_ (only before). Anybody assigning to background_thread_shutting_down_
  // must do so and join the thread while holding this mutex.
  mutable std::mutex thread_creation_mutex_;
  std::thread background_thread_;
  bool background_thread_shutting_down_ = false;
};

}  // namespace editor
}  // namespace afc

#endif  // __AFC_EDITOR_ASYNC_PROCESSOR_H__
