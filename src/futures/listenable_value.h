#ifndef __AFC_FUTURES_LISTENABLE_FUTURES_H__
#define __AFC_FUTURES_LISTENABLE_FUTURES_H__

#include <functional>
#include <memory>
#include <optional>
#include <vector>

#include "src/concurrent/protected.h"
#include "src/futures/futures.h"
#include "src/language/error/value_or_error.h"
#include "src/language/once_only_function.h"
#include "src/language/overload.h"
#include "src/language/safe_types.h"

namespace afc::futures {
// Similar to Value, but allows us to queue multiple listeners. The listeners
// receive the value by const-ref.
//
// This class is thread-safe.
template <typename Type>
class ListenableValue {
 public:
  using Listener = std::function<void(const Type&)>;

 private:
  struct Data {
    // Once it becomes set, never changes.
    std::optional<Type> value;
    std::vector<Listener> listeners;
  };

  language::NonNull<std::shared_ptr<concurrent::Protected<Data>>> data_ =
      language::MakeNonNullShared<concurrent::Protected<Data>>();

 public:
  ListenableValue(Value<Type> value) {
    std::move(value).Transform([shared_data = data_](Type immediate_value) {
      std::vector<std::function<void()>> callbacks;
      shared_data->lock([&](Data& data) {
        CHECK(!data.value.has_value());
        data.value = std::move(immediate_value);
        for (Listener& l : data.listeners) {
          callbacks.push_back(
              [l = std::move(l), &value = data.value.value()] mutable {
                std::move(l)(value);
              });
        }
        data.listeners.clear();
      });
      for (auto& l : callbacks) std::move(l)();
      return language::EmptyValue();
    });
  }

  void AddListener(Listener listener) const {
    data_->lock([&](Data& data) {
      if (data.value.has_value()) {
        std::move(listener)(data.value.value());
      } else {
        data.listeners.push_back(std::move(listener));
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
};

template <typename T>
futures::ValueOrError<T> ToFuture(
    language::ValueOrError<ListenableValue<T>> input) {
  return std::visit(
      language::overload{
          [](language::Error error) {
            return futures::Past(language::ValueOrError<T>(error));
          },
          [](ListenableValue<T> listenable_value) {
            return listenable_value.ToFuture().Transform(
                [](T t) { return language::Success(std::move(t)); });
          }},
      input);
}

}  // namespace afc::futures
#endif  // __AFC_FUTURES_LISTENABLE_FUTURES_H__
