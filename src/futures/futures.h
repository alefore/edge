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

#include "src/concurrent/protected.h"
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
    std::visit(
        language::overload{[&](language::Error error) { consumer(error); },
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
  static constexpr bool value_internal(C::IsFutureTag*) {
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

  using Consumer = std::function<void(Type)>;
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
        [consumer = output.consumer,
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
      Consumer consumer;

      data_.lock([&](Data& data) {
        CHECK(!data.value.has_value());
        CHECK(!data.consumer.has_value() || data.consumer.value() != nullptr);
        if (data.consumer.has_value()) {
          std::swap(consumer, *data.consumer);
        } else {
          data.value.emplace(std::move(final_value));
        }
      });
      if (consumer != nullptr) consumer(std::move(final_value));
    }

    void SetConsumer(Consumer final_consumer) {
      data_.lock([&](Data& data) {
        CHECK(!data.consumer.has_value());
        if (data.value.has_value()) {
          final_consumer(std::move(*data.value));
          data.consumer = nullptr;
          data.value = std::nullopt;
        } else {
          data.consumer = std::move(final_consumer);
        }
      });
    }

   private:
    struct Data {
      // std::nullopt before a consumer is set. nullptr when a consumer has
      // already been executed.
      std::optional<Consumer> consumer;
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

  // TODO(2022-06-10): Replace this with an instance of some class that
  // references the FutureData. The corresponding method in that class should
  // contain a single method with the && annotation.
  //
  // This is hard: many functions capture consumers; the requirement on
  // std::function to be copyable makes that difficult. It may become easier
  // once std::move_only_function (from C++23) becomes available; once that's
  // in, we can use that for the callbacks.
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
                                   Type value_or_error) {
    std::visit(language::overload{
                   [&](language::Error error) {
                     error_callback(error).SetConsumer(consumer);
                   },
                   [&](typename std::variant_alternative_t<0, Type> immediate) {
                     consumer(std::move(immediate));
                   }},
               std::move(value_or_error));
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
Value<IterationControlCommand> ForEach(Iterator input_begin, Iterator end,
                                       Callable callable) {
  Future<IterationControlCommand> output;
  auto resume_external = [consumer = output.consumer, end, callable](
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
  resume_external(input_begin, resume_external);
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
  auto resume_external = [consumer = output.consumer,
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
  resume_external(resume_external);
  return std::move(output.value);
}

Value<language::PossibleError> IgnoreErrors(
    Value<language::PossibleError> value);

// If value evaluates to an error, runs error_callback. error_callback will
// receive the error and should return a ValueOrError<T> to replace it. If it
// wants to preserve the error, it can just return it.
template <typename T, typename Callable>
ValueOrError<T> OnError(ValueOrError<T> value, Callable error_callback) {
  Future<language::ValueOrError<T>> future;
  std::move(value).SetConsumer([consumer = std::move(future.consumer),
                                error_callback = std::move(error_callback)](
                                   language::ValueOrError<T> value_or_error) {
    if (std::holds_alternative<language::Error>(value_or_error)) {
      futures::ValueOrError<T> error_callback_result =
          error_callback(std::get<language::Error>(std::move(value_or_error)));
      std::move(error_callback_result).SetConsumer(std::move(consumer));
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
