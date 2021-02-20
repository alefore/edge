// Simple settable futures implementation.
//
// Usage:
//
//   Futures::Future<X> my_future;
//
// Dispatch some async work:
//
//   Futures::Value<X>::Consumer consumer = my_future.consumer;
//   StartAsyncWork(consumer);
//
// When the async work is done:
//
//   X my_x = ComputeX(...);
//   consumer(my_x);
//
// The original caller will have returned:
//
//   Futures::Value<X> value = my_future.value;
//   return value;
//
// Customers of value can then schedule work to be executed when the value
// becomes known:
//
//   value.SetConsumer([](X x) { ... });
#ifndef __AFC_EDITOR_FUTURES_FUTURES_H__
#define __AFC_EDITOR_FUTURES_FUTURES_H__

#include <glog/logging.h>

#include <deque>
#include <functional>
#include <memory>
#include <type_traits>

#include "src/function_traits.h"
#include "src/value_or_error.h"

namespace afc::futures {

template <typename Type>
class Future;

// TODO(ms0): If A can be converted to type B, make it possible for Value<A> to
// be converted to Value<B> implicitly.
template <typename Type>
class Value {
 public:
  using Consumer = std::function<void(Type)>;
  using type = Type;

  const std::optional<Type>& Get() const { return data_->value; }

  void SetConsumer(Consumer consumer) {
    CHECK(!data_->consumer.has_value());
    if (data_->value.has_value()) {
      consumer(std::move(data_->value.value()));
      data_->consumer = nullptr;
      data_->value = std::nullopt;
    } else {
      data_->consumer = std::move(consumer);
    }
  }

 private:
  friend Future<Type>;

  struct FutureData {
    // std::nullopt before a consumer is set. nullptr when a consumer has
    // already been executed.
    std::optional<Consumer> consumer;
    std::optional<Type> value;
  };

  Value(std::shared_ptr<FutureData> data) : data_(std::move(data)) {}

  std::shared_ptr<FutureData> data_ = std::make_shared<FutureData>();
};

template <typename T>
using ValueOrError = Value<editor::ValueOrError<T>>;

// Similar to Value, but allows us to queue multiple listeners. The listeners
// receive the value by const-ref.
template <typename Type>
class ListenableValue {
 public:
  using Listener = std::function<void(const Type&)>;

  ListenableValue(Value<Type> value) {
    value.SetConsumer([data = data_](Type value) {
      data->value = std::move(value);
      for (auto& l : data->listeners) {
        l(data->value.value());
      }
      data->listeners.clear();
    });
  }

  void AddListener(Listener listener) {
    if (data_->value.has_value()) {
      listener(data_->value.value());
    } else {
      data_->listeners.push_back(listener);
    }
  }

 private:
  struct Data {
    std::optional<Type> value;
    std::deque<std::function<void(const Type&)>> listeners;
  };
  std::shared_ptr<Data> data_ = std::make_shared<Data>();
};

template <typename Type>
struct Future {
 public:
  Future() : Future(std::make_shared<FutureData>()) {}

  typename Value<Type>::Consumer consumer;
  Value<Type> value;

 private:
  using FutureData = typename Value<Type>::FutureData;

  Future(std::shared_ptr<FutureData> data)
      : consumer([data](Type value) {
          CHECK(!data->value.has_value());
          CHECK(!data->consumer.has_value() ||
                data->consumer.value() != nullptr);
          if (data->consumer.has_value()) {
            data->consumer.value()(std::move(value));
            data->consumer = nullptr;
          } else {
            data->value.emplace(std::move(value));
          }
        }),
        value(std::move(data)) {}
};

template <typename Type>
static Value<Type> Past(Type value) {
  Future<Type> output;
  output.consumer(std::move(value));
  return output.value;
}

enum class IterationControlCommand { kContinue, kStop };

// Evaluate `callable` for each element in the range [begin, end). `callable`
// receives a reference to each element and must return a
// Value<IterationControlCommand>.
//
// The returned value can be used to check whether the entire evaluation
// succeeded and/or to detect when it's finished.
//
// Must ensure that the iterators won't expire before the iteration is done. If
// that's a problem, `ForEachWithCopy` is provided below (which will make a copy
// of the entire container).
template <typename Iterator, typename Callable>
Value<IterationControlCommand> ForEach(Iterator begin, Iterator end,
                                       Callable callable) {
  Future<IterationControlCommand> output;
  auto resume = [consumer = output.consumer, end, callable](
                    Iterator begin, auto resume) mutable {
    if (begin == end) {
      consumer(IterationControlCommand::kContinue);
      return;
    }
    callable(*begin).SetConsumer([consumer, begin, end, callable, resume](
                                     IterationControlCommand result) mutable {
      if (result == IterationControlCommand::kStop) {
        consumer(result);
      } else {
        resume(++begin, resume);
      }
    });
  };
  resume(begin, resume);
  return output.value;
}

template <typename Callable>
Value<IterationControlCommand> While(Callable callable) {
  Future<IterationControlCommand> output;
  auto resume = [consumer = output.consumer,
                 callable](auto resume) mutable -> void {
    callable().SetConsumer([consumer = std::move(consumer),
                            callable = std::move(callable),
                            resume](IterationControlCommand result) mutable {
      if (result == IterationControlCommand::kStop) {
        consumer(result);
      } else {
        resume(resume);
      }
    });
  };
  resume(resume);
  return output.value;
}

template <typename>
struct TransformTraitsCallableReturn;

template <typename T>
struct TransformTraitsCallableReturn {
  using Type = Value<T>;
  static void Feed(T output, typename Value<T>::Consumer consumer) {
    consumer(std::move(output));
  }
};

template <typename T>
struct TransformTraitsCallableReturn<Value<T>> {
  using Type = Value<T>;
  static void Feed(Value<T> output, typename Value<T>::Consumer consumer) {
    output.SetConsumer(std::move(consumer));
  }
};

template <typename, typename>
struct TransformTraits;

template <class InitialType, class Callable>
struct TransformTraits {
  using ReturnTraits =
      TransformTraitsCallableReturn<decltype(std::declval<Callable>()(
          std::declval<InitialType>()))>;
  using ReturnType = typename ReturnTraits::Type;

  static void FeedValue(InitialType initial_value, Callable& callable,
                        typename ReturnType::Consumer consumer) {
    ReturnTraits::Feed(callable(std::move(initial_value)), std::move(consumer));
  }
};

template <class InitialType, class Callable>
struct TransformTraits<editor::ValueOrError<InitialType>, Callable> {
  using ReturnTraits =
      TransformTraitsCallableReturn<decltype(std::declval<Callable>()(
          std::declval<InitialType>()))>;
  using ReturnType = typename ReturnTraits::Type;

  static void FeedValue(editor::ValueOrError<InitialType> initial_value,
                        Callable& callable,
                        typename ReturnType::Consumer consumer) {
    if (initial_value.IsError()) {
      consumer(initial_value.error());
    } else {
      ReturnTraits::Feed(callable(std::move(initial_value.value())),
                         std::move(consumer));
    }
  }
};

template <typename InitialType, typename Callable>
auto Transform(Value<InitialType> delayed_value, Callable callable) {
  using Traits = TransformTraits<InitialType, Callable>;
  Future<typename Traits::ReturnType::type> output;
  delayed_value.SetConsumer(
      [consumer = output.consumer,
       callable = std::move(callable)](InitialType initial_value) mutable {
        Traits::FeedValue(std::move(initial_value), callable, consumer);
      });
  return output.value;
}

Value<editor::PossibleError> IgnoreErrors(Value<editor::PossibleError> value);

// If value evaluates to an error, runs error_callback. error_callback will
// receive the error and should return a ValueOrError<T> to replace it. If it
// wants to preserve the error, it can just return it.
//
// TODO(easy): error_callback should receive the error directly (not the
// ValueOrError).
template <typename T, typename Callable>
ValueOrError<T> OnError(ValueOrError<T> value, Callable error_callback) {
  Future<editor::ValueOrError<T>> future;
  value.SetConsumer([consumer = std::move(future.consumer),
                     error_callback = std::move(error_callback)](
                        editor::ValueOrError<T> value_or_error) {
    consumer(value_or_error.IsError()
                 ? error_callback(std::move(value_or_error.error()))
                 : std::move(value_or_error));
  });
  return future.value;
}

template <typename InitialType, typename Type>
auto Transform(Value<InitialType> delayed_value, Value<Type> value) {
  return Transform(delayed_value,
                   [value = std::move(value)](
                       const InitialType&) -> Value<Type> { return value; });
}

template <typename T0>
auto Transform(Value<T0> t0) {
  return t0;
}

template <typename T0, typename T1, typename... Args>
auto Transform(Value<T0> t0, T1 t1, Args... args) {
  return Transform(Transform(std::move(t0), std::move(t1)), args...);
}

template <typename Iterator, typename Callable>
Value<IterationControlCommand> ForEachWithCopy(Iterator begin, Iterator end,
                                               Callable callable) {
  auto copy = std::make_shared<std::vector<typename std::remove_const<
      typename std::remove_reference<decltype(*begin)>::type>::type>>(begin,
                                                                      end);
  return futures::Transform(
      ForEach(copy->begin(), copy->end(), std::move(callable)),
      [copy](IterationControlCommand output) { return output; });
}

}  // namespace afc::futures

#endif  // __AFC_EDITOR_FUTURES_FUTURES_H__
