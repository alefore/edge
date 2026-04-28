#ifndef __AFC_EDITOR_SRC_FUTURES_PROGRESSIVE_H__
#define __AFC_EDITOR_SRC_FUTURES_PROGRESSIVE_H__

#include "src/concurrent/protected.h"
#include "src/futures/listenable_value.h"
#include "src/language/error/value_or_error.h"
#include "src/language/safe_types.h"

namespace afc::futures {

// T must be a copyable type.
template <typename T>
class Progressive {
  language::NonNull<std::shared_ptr<concurrent::Protected<T>>> value_;
  // Notified when we know that value_ holds the final data.
  futures::ListenableValue<language::EmptyValue> final_value_future_;

 public:
  explicit Progressive(T initial_value)
      : Progressive(initial_value, initial_value) {}

  explicit Progressive(T initial_value, Value<T> future)
      : value_(language::MakeNonNullShared<concurrent::Protected<T>>(
            std::move(initial_value))),
        final_value_future_(std::move(future).Transform([value = value_](
                                                            T final_value) {
          value->lock([&](T& storage) { storage = std::move(final_value); });
          return language::EmptyValue{};
        })) {}

  // Returns the best known value (`value` if already available; otherwise
  // `provisional_value`).
  T current() const {
    return value_->lock([](const T& value) { return value; });
  }

  // Returns a future notified with the final value.
  futures::Value<T> final() const {
    return final_value_future_.ToFuture().Transform(
        [value_ptr = value_](language::EmptyValue) {
          return value_ptr->lock([](const T& value) { return value; });
        });
  }
};
}  // namespace afc::futures
namespace std {
template <typename T>
struct hash<afc::futures::Progressive<T>> {
  std::size_t operator()(const afc::futures::Progressive<T>& p) const {
    return hash<T>{}(p.current());
  }
};
}  // namespace std
#endif  // __AFC_EDITOR_SRC_FUTURES_PROGRESSIVE_H__
