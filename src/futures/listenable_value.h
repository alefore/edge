#ifndef __AFC_FUTURES_LISTENABLE_FUTURES_H__
#define __AFC_FUTURES_LISTENABLE_FUTURES_H__

namespace afc::futures {
// Similar to Value, but allows us to queue multiple listeners. The listeners
// receive the value by const-ref.
//
// This class is thread-safe.
template <typename Type>
class ListenableValue {
 public:
  using Listener = std::function<void(const Type&)>;

  ListenableValue(Value<Type> value) {
    std::move(value).SetConsumer([shared_data = data_](Type immediate_value) {
      std::vector<std::function<void()>> callbacks;
      shared_data->lock([&](Data& data) {
        CHECK(!data.value.has_value());
        data.value = std::move(immediate_value);
        for (Listener& l : data.listeners) {
          callbacks.push_back(
              [l = std::move(l), &value = data.value.value()] { l(value); });
        }
        data.listeners.clear();
      });
      for (auto& l : callbacks) l();
    });
  }

  void AddListener(Listener listener) const {
    data_->lock([&](Data& data) {
      if (data.value.has_value()) {
        listener(data.value.value());
      } else {
        data.listeners.push_back(listener);
      }
    });
  }

  bool has_value() const {
    return data_->lock(
        [&](const Data& data) { return data.value.has_value(); });
  }

  template <typename Callable>
  auto lock(Callable callable) const {
    return data_->lock([&](const Data& data) { return callable(data.value); });
  }

  std::optional<Type> get_copy() const {
    return data_->lock([&](const Data& data) { return data.value; });
  }

  template <typename T = Type>
  Value<Type> ToFuture() const {
    Future<T> output;
    AddListener(std::move(output.consumer));
    return std::move(output.value);
  }

 private:
  struct Data {
    // Once it becomes set, never changes.
    std::optional<Type> value;
    std::vector<std::function<void(const Type&)>> listeners;
  };

  std::shared_ptr<concurrent::Protected<Data>> data_ =
      std::make_shared<concurrent::Protected<Data>>();
};
}  // namespace afc::futures
#endif  // __AFC_FUTURES_LISTENABLE_FUTURES_H__
