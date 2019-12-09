#ifndef __AFC_EDITOR_ASYNC_PROCESSOR_H__
#define __AFC_EDITOR_ASYNC_PROCESSOR_H__

#include <chrono>
#include <condition_variable>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>

#include "glog/logging.h"

namespace afc::editor {

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

  struct Options {
    Factory factory;
    NotifyCallback notify_callback = [] {};

    // Controls the behavior when `Push` is called while there are unprocessed
    // inputs.
    enum class QueueBehavior { kFlush, kWait };
    QueueBehavior push_behavior = QueueBehavior::kFlush;

    std::wstring name;
  };

  AsyncProcessor(Options options)
      : options_([&] {
          if (options.name.empty()) {
            options.name = L"<anonymous>";
          }
          return options;
        }()) {}

  ~AsyncProcessor() { PauseThread(); }

  void Push(Input input) {
    thread_creation_mutex_.lock();
    mutex_.lock();
    if (options_.push_behavior == Options::QueueBehavior::kFlush) {
      input_queue_.clear();
    }
    input_queue_.push_back(std::move(input));
    switch (state_) {
      case State::kNotRunning: {
        LOG(INFO) << options_.name << ": Creating thread";
        if (background_thread_.joinable()) {
          background_thread_.join();
        }
        state_ = State::kRunning;
        background_thread_ = std::thread([this]() { BackgroundThread(); });
        break;
      }
      case State::kTerminationRequested:
        state_ = State::kRunning;
        break;
      case State::kRunning:
        break;
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
    std::unique_lock<std::mutex> lock(mutex_);
    if (state_ == State::kNotRunning) {
      if (background_thread_.joinable()) {
        background_thread_.join();
      }
      return;
    }

    state_ = State::kTerminationRequested;
    lock.unlock();

    background_condition_.notify_one();
    background_thread_.join();
    CHECK(state_ == State::kNotRunning);
  }

 private:
  void BackgroundThread() {
    using namespace std::chrono_literals;
    while (true) {
      std::unique_lock<std::mutex> lock(mutex_);
      CHECK(state_ != State::kNotRunning);
      background_condition_.wait_for(lock, 2s, [this]() {
        return state_ != State::kRunning || !input_queue_.empty();
      });
      CHECK(state_ != State::kNotRunning);
      VLOG(5) << options_.name << ": Background thread is waking up.";
      if (input_queue_.empty()) {
        LOG(INFO) << options_.name << ": Background thread is shutting down.";
        state_ = State::kNotRunning;
        return;
      }

      Input input = std::move(input_queue_.front());
      input_queue_.pop_front();

      lock.unlock();

      const Output output = options_.factory(std::move(input));

      mutex_.lock();
      output_ = std::move(output);
      mutex_.unlock();

      options_.notify_callback();
    }
  }

  const Options options_;

  // If non-empty, pending input for the background thread to pick up.
  std::deque<Input> input_queue_;

  // As soon as the first execution of the supplier completes, we store here
  // the value it returns. Whenever it finishes, we update the previous value.
  std::optional<Output> output_;

  // Protects all the variables that background thread may access.
  mutable std::mutex mutex_;
  std::condition_variable background_condition_;

  enum class State { kRunning, kNotRunning, kTerminationRequested };
  State state_ = State::kNotRunning;

  // Protects access to background_thread_ itself. Must never be acquired
  // after mutex_ (only before). Anybody assigning to
  // background_thread_shutting_down_ must do so and join the thread while
  // holding this mutex.
  mutable std::mutex thread_creation_mutex_;
  std::thread background_thread_;
};

}  // namespace afc::editor

#endif  // __AFC_EDITOR_ASYNC_PROCESSOR_H__
