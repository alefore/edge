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

#include "src/language/function_traits.h"
#include "src/language/value_or_error.h"

namespace afc::futures {

template <typename Type>
class Future;

template <typename Type>
class Value;

enum class IterationControlCommand { kContinue, kStop };

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
struct TransformTraits<language::ValueOrError<InitialType>, Callable> {
  using ReturnTraits =
      TransformTraitsCallableReturn<decltype(std::declval<Callable>()(
          std::declval<InitialType>()))>;
  using ReturnType = typename ReturnTraits::Type;

  static void FeedValue(language::ValueOrError<InitialType> initial_value,
                        Callable& callable,
                        typename ReturnType::Consumer consumer) {
    std::visit(
        language::overload{[&](language::Error error) { consumer(error); },
                           [&](InitialType value) {
                             ReturnTraits::Feed(callable(std::move(value)),
                                                std::move(consumer));
                           }},
        std::move(initial_value));
  }
};

// TODO(ms0): If A can be converted to type B, make it possible for Value<A> to
// be converted to Value<B> implicitly.
template <typename Type>
class Value {
 public:
  Value(const Value<Type>&) = delete;
  Value(Value<Type>&&) = default;
  Value<Type>& operator=(Value<Type>&&) = default;

  template <typename Other>
  Value(Value<Other> other) {
    other.SetConsumer([data = data_](Other other_value) {
      data->Feed(std::move(other_value));
    });
  }

  using Consumer = std::function<void(Type)>;
  using type = Type;

  const std::optional<Type>& Get() const { return data_->read(); }

  void SetConsumer(Consumer consumer) {
    data_->SetConsumer(std::move(consumer));
  }

  template <typename Callable>
  auto Transform(Callable callable) {
    using Traits = TransformTraits<Type, Callable>;
    Future<typename Traits::ReturnType::type> output;
    SetConsumer([consumer = output.consumer,
                 callable = std::move(callable)](Type initial_value) mutable {
      Traits::FeedValue(std::move(initial_value), callable, consumer);
    });
    return std::move(output.value);
  }

  // Turns a futures::Value<ValueOrError<T>> into a futures::Value<T>; if the
  // future has errors, uses the callable (which receives the error) to turn
  // them into a value (and ignore them).
  //
  // Example:
  //
  // futures::Value<int> value = futures::Past(futures::ValueOrError<int>(...))
  //     .ConsumeErrors([](Error error) { ... return 0; });
  template <typename Callable>
  auto ConsumeErrors(Callable error_callback);

 private:
  friend Future<Type>;

  class FutureData {
   public:
    FutureData() = default;
    const std::optional<Type>& read() { return value_; }

    void Feed(Type final_value) {
      CHECK(!value_.has_value());
      CHECK(!consumer_.has_value() || consumer_.value() != nullptr);
      if (consumer_.has_value()) {
        (*consumer_)(std::move(final_value));
        consumer_ = nullptr;
      } else {
        value_.emplace(std::move(final_value));
      }
    }

    void SetConsumer(Consumer final_consumer) {
      CHECK(!consumer_.has_value());
      if (value_.has_value()) {
        final_consumer(std::move(*value_));
        consumer_ = nullptr;
        value_ = std::nullopt;
      } else {
        consumer_ = std::move(final_consumer);
      }
    }

   private:
    // std::nullopt before a consumer is set. nullptr when a consumer has
    // already been executed.
    std::optional<Consumer> consumer_;
    std::optional<Type> value_;
  };

  Value(std::shared_ptr<FutureData> data) : data_(std::move(data)) {}

  std::shared_ptr<FutureData> data_ = std::make_shared<FutureData>();
};

template <typename T>
using ValueOrError = Value<language::ValueOrError<T>>;

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

  const std::optional<Type>& get() const { return data_->value; }

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
      : consumer([data](Type value) { data->Feed(std::move(value)); }),
        value(std::move(data)) {}
};

// TODO(easy, 2022-05-29): This should be defined on Value<ValueOrError<Type>>,
// adjusting all expressions to match.
template <typename Type>
template <typename Callable>
auto Value<Type>::ConsumeErrors(Callable error_callback) {
  Future<typename std::variant_alternative_t<0, Type>> output;
  SetConsumer(
      [consumer = std::move(output.consumer),
       error_callback = std::move(error_callback)](Type value_or_error) {
        if (std::holds_alternative<language::Error>(value_or_error)) {
          error_callback(std::get<language::Error>(std::move(value_or_error)))
              .SetConsumer(consumer);
        } else {
          consumer(std::get<typename std::variant_alternative_t<0, Type>>(
              std::move(value_or_error)));
        }
      });
  return std::move(output.value);
}

template <typename Type>
static Value<Type> Past(Type value) {
  Future<Type> output;
  output.consumer(std::move(value));
  return std::move(output.value);
}

// Evaluate `callable` for each element in the range [begin, end). `callable`
// receives a reference to each element and must return a
// Value<IterationControlCommand>.
//
// The returned value can be used to check whether the entire evaluation
// succeeded and/or to detect when it's finished.
//
// Must ensure that the iterators won't expire before the iteration is done.
// Customers are encouraged to use the `ForEach(std::shared_ptr<Container>,
// ...)` version provided below, when feasible.
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
  return std::move(output.value);
}

// Version of ForEach optimized for the case where the customer has a shared_ptr
// to the container; this will take care of capturing the reference to the
// container.
//
// Unlike ForEachWithCopy, avoids having to copy the container.
template <typename Container, typename Callable>
Value<IterationControlCommand> ForEach(std::shared_ptr<Container> container,
                                       Callable callable) {
  return ForEach(container->begin(), container->end(),
                 [container, callable](auto& T) { return callable(T); });
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
  return std::move(output.value);
}

Value<language::PossibleError> IgnoreErrors(
    Value<language::PossibleError> value);

// If value evaluates to an error, runs error_callback. error_callback will
// receive the error and should return a ValueOrError<T> to replace it. If it
// wants to preserve the error, it can just return it.
//
// TODO(easy): error_callback should receive the error directly (not the
// ValueOrError).
template <typename T, typename Callable>
ValueOrError<T> OnError(ValueOrError<T>&& value, Callable error_callback) {
  Future<language::ValueOrError<T>> future;
  value.SetConsumer([consumer = std::move(future.consumer),
                     error_callback = std::move(error_callback)](
                        language::ValueOrError<T> value_or_error) {
    if (std::holds_alternative<language::Error>(value_or_error)) {
      futures::ValueOrError<T> error_callback_result =
          error_callback(std::get<language::Error>(std::move(value_or_error)));
      error_callback_result.SetConsumer(std::move(consumer));
    } else {
      consumer(std::move(value_or_error));
    }
  });
  return std::move(future.value);
}

template <typename Iterator, typename Callable>
Value<IterationControlCommand> ForEachWithCopy(Iterator begin, Iterator end,
                                               Callable callable) {
  auto copy = std::make_shared<std::vector<typename std::remove_const<
      typename std::remove_reference<decltype(*begin)>::type>::type>>();
  while (begin != end) {
    copy->push_back(std::move(*begin));
    ++begin;
  }
  return ForEach(copy->begin(), copy->end(), std::move(callable))
      .Transform([copy](IterationControlCommand output) { return output; });
}

#define FUTURES_ASSIGN_OR_RETURN(variable, expression)                         \
  variable = ({                                                                \
    auto tmp = expression;                                                     \
    if (afc::language::Error* error = std::get_if<afc::language::Error>(&tmp); \
        error != nullptr)                                                      \
      return futures::Past(std::move(*error));                                 \
    std::get<0>(std::move(tmp));                                               \
  })

}  // namespace afc::futures

#endif  // __AFC_EDITOR_FUTURES_FUTURES_H__
