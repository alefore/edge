#include <functional>

#ifndef __AFC_EDITOR_CONTINUATION_H__
#define __AFC_EDITOR_CONTINUATION_H__

namespace afc::editor {
// Evaluate `callable` for each element in the range [begin, end). `callable`
// must receive two elements: the dereferenced iterator, and the callback that
// it should use to resume the evaluation.
template <typename Iterator, typename Callable, typename Done>
void ForEach(Iterator begin, Iterator end, Callable callable, Done done) {
  if (begin == end) {
    done();
  } else {
    callable(*begin(), [begin, end, callable, done]() {
      ForEach(++begin, end, callable, done);
    });
  }
}

template <typename Type>
class ValueReceiver;

template <typename Type>
class Future;

template <typename Type>
class DelayedValue {
 public:
  using Listener = std::function<void(const Type&)>;

  template <typename OtherType, typename Callable>
  static DelayedValue<Type> Transform(DelayedValue<OtherType> delayed_value,
                                      Callable callable) {
    Future<Type> output;
    delayed_value.AddListener(
        [receiver = output.Receiver(),
         callable = std::move(callable)](const OtherType& other_type) mutable {
          receiver.Set(callable(other_type));
        });
    return output.Value();
  }

  DelayedValue<Type>(Type value) { data_->value.emplace(std::move(value)); }

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
}  // namespace afc::editor

#endif  // __AFC_EDITOR_CONTINUATION_H__
