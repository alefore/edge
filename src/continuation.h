#include <functional>

#ifndef __AFC_EDITOR_CONTINUATION_H__
#define __AFC_EDITOR_CONTINUATION_H__

namespace afc::editor {

template <typename Type>
class ValueReceiver;

template <typename Type>
class Future;

template <typename Type>
class DelayedValue {
 public:
  using Listener = std::function<void(const Type&)>;

  // Callable must return a DelayedValue<Type> given an OtherType value.
  template <typename OtherType, typename Callable>
  static DelayedValue<Type> Transform(DelayedValue<OtherType> delayed_value,
                                      Callable callable) {
    Future<Type> output;
    delayed_value.AddListener([receiver = output.Receiver(),
                               callable = std::move(callable)](
                                  const OtherType& other_value) mutable {
      callable(other_value).AddListener([receiver](const Type& value) mutable {
        receiver.Set(value);
      });
    });
    return output.Value();
  }

  const std::optional<Type>& Get() const { return data_->value; }
  void AddListener(Listener listener) {
    if (data_->value.has_value()) {
      listener(data_->value.value());
    } else {
      data_->listeners.push_back(std::move(listener));
    }
  }

 private:
  friend ValueReceiver<Type>;
  friend Future<Type>;

  struct FutureData {
    std::vector<Listener> listeners;
    std::optional<Type> value;
  };

  DelayedValue(std::shared_ptr<FutureData> data) : data_(std::move(data)) {}

  std::shared_ptr<FutureData> data_ = std::make_shared<FutureData>();
};

enum class ValueReceiverSetResult { kAccepted, kRejected };

template <typename Type>
class ValueReceiver {
 public:
  ValueReceiverSetResult Set(Type value) {
    if (data_->value.has_value()) {
      return ValueReceiverSetResult::kRejected;
    }
    data_->value.emplace(std::move(value));
    for (const auto& l : data_->listeners) {
      l(data_->value.value());
    }
    data_->listeners.empty();
    return ValueReceiverSetResult::kAccepted;
  }

 private:
  friend Future<Type>;

  using FutureData = typename DelayedValue<Type>::FutureData;
  ValueReceiver(std::shared_ptr<FutureData> data) : data_(std::move(data)) {}

  std::shared_ptr<FutureData> data_ = std::make_shared<FutureData>();
};

template <typename Type>
class Future {
 public:
  static ValueReceiver<Type> ReceiverForListener(
      typename DelayedValue<Type>::Listener listener) {
    Future<Type> output;
    output.Value().AddListener(std::move(listener));
    return output.Receiver();
  }

  ValueReceiver<Type> Receiver() { return ValueReceiver<Type>(data_); }

  DelayedValue<Type> Value() { return DelayedValue<Type>(data_); }

 private:
  using FutureData = typename DelayedValue<Type>::FutureData;

  std::shared_ptr<FutureData> data_ = std::make_shared<FutureData>();
};

namespace futures {
template <typename Type>
DelayedValue<Type> ImmediateValue(Type value) {
  Future<Type> output;
  output.Receiver().Set(std::move(value));
  return output.Value();
}

enum class IterationControlCommand { kContinue, kStop };

// Evaluate `callable` for each element in the range [begin, end). `callable`
// receives a reference to each element and must return a
// DelayedValue<IterationControlCommand>.
//
// The returned value can be used to check whether the entire evaluation
// succeeded and/or to detect when it's finished.
template <typename Iterator, typename Callable>
DelayedValue<IterationControlCommand> ForEach(Iterator begin, Iterator end,
                                              Callable callable) {
  Future<IterationControlCommand> output;
  auto resume = [receiver = output.Receiver(), end, callable](
                    Iterator begin, auto resume) mutable {
    if (begin == end) {
      receiver.Set(IterationControlCommand::kContinue);
      return;
    }
    callable(*begin).AddListener([receiver, begin, end, callable, resume](
                                     IterationControlCommand result) mutable {
      if (result == IterationControlCommand::kStop) {
        receiver.Set(result);
      } else {
        resume(++begin, resume);
      }
    });
  };
  resume(begin, resume);
  return output.Value();
}

template <typename Callable>
DelayedValue<IterationControlCommand> While(Callable callable) {
  Future<IterationControlCommand> output;
  auto resume = [receiver = output.Receiver(),
                 callable](auto resume) mutable -> void {
    callable().AddListener(
        [receiver, callable, resume](IterationControlCommand result) mutable {
          if (result == IterationControlCommand::kStop) {
            receiver.Set(result);
          } else {
            resume(resume);
          }
        });
  };
  resume(resume);
  return output.Value();
}
}  // namespace futures
}  // namespace afc::editor

#endif  // __AFC_EDITOR_CONTINUATION_H__
