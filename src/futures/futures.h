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
//   std::move(consumer)(my_x);
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

#include "src/concurrent/protected.h"
#include "src/language/error/value_or_error.h"
#include "src/language/once_only_function.h"
#include "src/language/overload.h"
#include "src/language/safe_types.h"

namespace afc::futures {

template <typename Type>
struct Future;

template <typename Type>
class Value;

enum class IterationControlCommand { kContinue, kStop };

template <typename>
struct TransformTraitsCallableReturn;

template <typename T>
struct TransformTraitsCallableReturn {
  using Type = Value<T>;
  static void Feed(T output, typename Value<T>::Consumer consumer) {
    std::move(consumer)(std::move(output));
  }
};

template <typename T>
struct TransformTraitsCallableReturn<Value<T>> {
  using Type = Value<T>;
  static void Feed(Value<T> output, typename Value<T>::Consumer consumer) {
    std::move(output).SetConsumer(std::move(consumer));
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
    std::visit(language::overload{
                   [&](language::Error error) { std::move(consumer)(error); },
                   [&](InitialType value) {
                     ReturnTraits::Feed(callable(std::move(value)),
                                        std::move(consumer));
                   }},
               std::move(initial_value));
  }
};

template <class T>
struct is_future {
 private:
  template <typename C>
  static constexpr bool value_internal(typename C::IsFutureTag*) {
    return true;
  };

  template <typename C>
  static constexpr bool value_internal(C*) {
    return false;
  };

 public:
  static constexpr bool value = value_internal<T>(nullptr);
};

template <typename Type>
class Value {
 public:
  using IsFutureTag = Type;

  Value(const Value<Type>&) = delete;
  Value(Value<Type>&&) = default;
  Value<Type>& operator=(Value<Type>&&) = default;

  template <typename Other>
  Value(Value<Other> other) {
    std::move(other).SetConsumer([data = data_](Other other_immediate) {
      data->Feed(std::move(other_immediate));
    });
  }

  using Consumer = language::OnceOnlyFunction<void(Type)>;
  using type = Type;

  bool has_value() const { return data_->has_value(); }
  std::optional<Type> Get() const { return data_->read(); }

  void SetConsumer(Consumer consumer) && {
    data_->SetConsumer(std::move(consumer));
  }

  template <typename Callable>
  auto Transform(Callable callable) && {
    using Traits = TransformTraits<Type, Callable>;
    Future<typename Traits::ReturnType::type> output;
    std::move(*this).SetConsumer(
        [consumer = std::move(output.consumer),
         callable = std::move(callable)](Type initial_value) mutable {
          Traits::FeedValue(std::move(initial_value), callable,
                            std::move(consumer));
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
  auto ConsumeErrors(Callable error_callback) &&;

 private:
  friend Future<Type>;

  // This class is thread-safe.
  class FutureData {
   public:
    FutureData() = default;

    std::optional<Type> read() {
      return data_.lock([](Data& data) { return data.value; });
    }

    bool has_value() const {
      return data_.lock(
          [](const Data& data) { return data.value.has_value(); });
    }

    void Feed(Type final_value) {
      auto consumer = data_.lock([&](Data& data) -> std::optional<Consumer> {
        CHECK(!data.value.has_value());
        return std::visit(
            language::overload{
                [&](ConsumerNotReceived) -> std::optional<Consumer> {
                  data.value.emplace(std::move(final_value));
                  return std::nullopt;
                },
                [&](Consumer& consumer_value) -> std::optional<Consumer> {
                  Consumer output = std::move(consumer_value);
                  data.consumer = ConsumerExecuted{};
                  return output;
                },
                [](ConsumerExecuted) -> std::optional<Consumer> {
                  LOG(FATAL) << "Received value after consumer has executed.";
                  return std::nullopt;
                },
            },
            data.consumer);
      });
      if (consumer.has_value()) std::move (*consumer)(std::move(final_value));
    }

    void SetConsumer(Consumer final_consumer) {
      data_.lock([&](Data& data) {
        CHECK(std::holds_alternative<ConsumerNotReceived>(data.consumer));
        if (data.value.has_value()) {
          std::move(final_consumer)(std::move(*data.value));
          data.consumer = ConsumerExecuted{};
          data.value = std::nullopt;
        } else {
          data.consumer = std::move(final_consumer);
        }
      });
    }

   private:
    struct ConsumerNotReceived {};
    struct ConsumerExecuted {};
    struct Data {
      std::variant<ConsumerNotReceived, Consumer, ConsumerExecuted> consumer =
          ConsumerNotReceived{};
      std::optional<Type> value;
    };
    concurrent::Protected<Data> data_;
  };

  Value(std::shared_ptr<FutureData> data) : data_(std::move(data)) {}

  std::shared_ptr<FutureData> data_ = std::make_shared<FutureData>();
};

template <typename T>
using ValueOrError = Value<language::ValueOrError<T>>;

template <typename Type>
struct Future {
 public:
  Future() : Future(std::make_shared<FutureData>()) {}

  typename Value<Type>::Consumer consumer;
  Value<Type> value;

 private:
  using FutureData = typename Value<Type>::FutureData;

  Future(std::shared_ptr<FutureData> data)
      : consumer([data](Type immediate_value) {
          data->Feed(std::move(immediate_value));
        }),
        value(std::move(data)) {}
};

// ConsumeErrors only makes sense if Type is a ValueOrError<>.
//
// It would be ideal to define that just for those future::Value<> instances,
// but that seems to be difficult. One alternative would be to define it as a
// top level symbol, but then we won't be able to chain calls.
template <typename Type>
template <typename Callable>
auto Value<Type>::ConsumeErrors(Callable error_callback) && {
  Future<typename std::variant_alternative_t<0, Type>> output;
  std::move(*this).SetConsumer([consumer = std::move(output.consumer),
                                error_callback = std::move(error_callback)](
                                   Type value_or_error) mutable {
    std::visit(language::overload{
                   [&](language::Error error) {
                     error_callback(error).SetConsumer(std::move(consumer));
                   },
                   [&](typename std::variant_alternative_t<0, Type> immediate) {
                     std::invoke(std::move(consumer), std::move(immediate));
                   }},
               std::move(value_or_error));
  });
  return std::move(output.value);
}

template <typename Type>
static Value<Type> Past(Type value) {
  Future<Type> output;
  std::move(output.consumer)(std::move(value));
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
  if (begin == end) return futures::Past(IterationControlCommand::kContinue);
  return callable(*begin).Transform(
      [begin, end, callable](IterationControlCommand result) mutable {
        if (result == IterationControlCommand::kStop)
          return futures::Past(result);
        return ForEach(++begin, end, callable);
      });
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
  return callable().Transform(
      [callable](IterationControlCommand result) mutable {
        if (result == IterationControlCommand::kStop)
          return futures::Past(result);
        return While(std::move(callable));
      });
}

Value<language::PossibleError> IgnoreErrors(
    Value<language::PossibleError> value);

// If value evaluates to an error, runs error_callback. error_callback will
// receive the error and should return a ValueOrError<T> to replace it. If it
// wants to preserve the error, it can just return it.
template <typename T, typename Callable>
ValueOrError<T> OnError(ValueOrError<T> value, Callable error_callback) {
  Future<language::ValueOrError<T>> future;
  std::move(value).SetConsumer(
      [consumer = std::move(future.consumer),
       error_callback = std::move(error_callback)](
          language::ValueOrError<T> value_or_error) mutable {
        if (std::holds_alternative<language::Error>(value_or_error)) {
          futures::ValueOrError<T> error_callback_result = error_callback(
              std::get<language::Error>(std::move(value_or_error)));
          std::move(error_callback_result).SetConsumer(std::move(consumer));
        } else {
          std::invoke(std::move(consumer), std::move(value_or_error));
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

// Combines futures (of possibly different values) into a single future with a
// tuple with the contained values.
template <typename T0, typename T1>
futures::Value<std::tuple<T0, T1>> JoinValues(futures::Value<T0> f0,
                                              futures::Value<T1> f1) {
  auto shared_f1 = MakeNonNullShared<futures::Value<T1>>(std::move(f1));
  return std::move(f0).Transform(
      [shared_f1 = std::move(shared_f1)](T0 t0) mutable {
        return std::move(shared_f1.value())
            .Transform([t0 = std::move(t0)](T1 t1) mutable {
              return std::tuple{std::move(t0), std::move(t1)};
            });
      });
}

// Turns a vector of futures into a future vector (of immediate values).
//
// std::vector<future::Value<X>>
// => future::Value<std::vector<X>>
template <typename Value>
futures::Value<std::vector<Value>> UnwrapVectorFuture(
    language::NonNull<std::shared_ptr<std::vector<futures::Value<Value>>>>
        input) {
  auto output = language::MakeNonNullShared<std::vector<Value>>();
  // TODO(2025-05-27, trivial): Remove need to call `get_shared()` below:
  return futures::ForEach(
             input.get_shared(),
             [output](futures::Value<Value>& future_item) {
               return std::move(future_item).Transform([output](Value item) {
                 output->push_back(std::move(item));
                 return IterationControlCommand::kContinue;
               });
             })
      .Transform(
          [output](IterationControlCommand) mutable { return output.value(); });
}
}  // namespace afc::futures

#endif  // __AFC_EDITOR_FUTURES_FUTURES_H__
