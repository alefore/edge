#include <glog/logging.h>

#include <functional>
#include <type_traits>

#ifndef __AFC_EDITOR_FUTURES_FUTURES_H__
#define __AFC_EDITOR_FUTURES_FUTURES_H__

namespace afc::futures {

template <typename Type>
class Future;

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

template <typename T>
struct TransformTraitsCallableReturn {
  using ReturnType = Value<T>;
  static void FeedValue(T output, typename Value<T>::Consumer consumer) {
    consumer(std::move(output));
  }
};

template <typename T>
struct TransformTraitsCallableReturn<Value<T>> {
  using ReturnType = Value<T>;
  static void FeedValue(Value<T> output, typename Value<T>::Consumer consumer) {
    output.SetConsumer(std::move(consumer));
  }
};

template <typename OtherType, typename Callable>
typename TransformTraitsCallableReturn<
    decltype(std::declval<Callable>()(std::declval<OtherType>()))>::ReturnType
Transform(Value<OtherType> delayed_value, Callable callable) {
  using Traits = TransformTraitsCallableReturn<decltype(
      std::declval<Callable>()(std::declval<OtherType>()))>;
  Future<typename Traits::ReturnType::type> output;
  delayed_value.SetConsumer(
      [consumer = output.consumer,
       callable = std::move(callable)](OtherType other_value) mutable {
        Traits::FeedValue(callable(std::move(other_value)), consumer);
      });
  return output.value;
}

template <typename OtherType, typename Type>
auto Transform(Value<OtherType> delayed_value, Value<Type> value) {
  return Transform(delayed_value,
                   [value = std::move(value)](const OtherType&) -> Value<Type> {
                     return value;
                   });
}

template <typename T0, typename T1, typename T2>
auto Transform(Value<T0> t0, T1 t1, T2 t2) {
  return Transform(Transform(std::move(t0), std::move(t1)), std::move(t2));
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
