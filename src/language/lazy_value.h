#ifndef __AFC_LANGUAGE_LAZY_VALUE_H__
#define __AFC_LANGUAGE_LAZY_VALUE_H__

#include <memory>
#include <type_traits>
#include <variant>

#include "src/concurrent/protected.h"
#include "src/language/once_only_function.h"
#include "src/language/overload.h"
#include "src/language/safe_types.h"

namespace afc::language {
// This class is thread-safe.
template <typename Value>
class LazyValue {
  using Factory = OnceOnlyFunction<Value()>;
  // We wrap it in NonNull to make this type copyable. Non-const to enable
  // moves.
  language::NonNull<
      std::shared_ptr<concurrent::Protected<std::variant<Value, Factory>>>>
      data_;

 public:
  explicit LazyValue(Factory factory)
      : data_(MakeNonNullShared<
              concurrent::Protected<std::variant<Value, Factory>>>(
            std::move(factory))) {}
  LazyValue(const LazyValue&) = default;
  LazyValue(LazyValue&&) = default;
  LazyValue& operator=(const LazyValue&) = default;
  LazyValue& operator=(LazyValue&&) = default;

  template <typename U, typename std::enable_if_t<
                            std::is_constructible_v<Value, U&&> &&
                                !std::is_same_v<std::decay_t<U>, LazyValue>,
                            int> = 0>
  LazyValue(U&& immediate)
      : data_(MakeNonNullShared<
              concurrent::Protected<std::variant<Value, Factory>>>(
            std::variant<Value, Factory>(std::in_place_index<0>,
                                         std::forward<U>(immediate)))) {}

  template <typename U, typename std::enable_if_t<
                            std::is_constructible_v<Value, const U&>, int> = 0>
  LazyValue(LazyValue<U> other)
      : data_(MakeNonNullShared<
              concurrent::Protected<std::variant<Value, Factory>>>(
            Factory([o = std::move(other)]() mutable -> Value {
              return o.get();  // Implicitly converts U to Value.
            }))) {}

  const Value& get() const {
    // Returning the locked data is safe here: we know that data_ will never
    // change (once it gets a value).
    return *data_->lock([](std::variant<Value, Factory>& data) -> const Value* {
      if (Factory* factory = std::get_if<Factory>(&data); factory != nullptr)
        data = std::invoke(std::move(*factory));
      // Why take the address if we'll just de-ref it? Because the compiler gets
      // confused and warns us about returning a reference to a temporary value.
      return &std::get<Value>(data);
    });
  }

  bool has_value() const {
    return data_->lock([](const std::variant<Value, Factory>& data) {
      return std::holds_alternative<Value>(data);
    });
  }
};

template <typename Callable>
auto MakeLazyValue(Callable callable) {
  return LazyValue<std::invoke_result_t<Callable>>{callable};
}

template <typename Callable>
LazyValue(Callable) -> LazyValue<std::invoke_result_t<Callable>>;

}  // namespace afc::language
#endif  // __AFC_LANGUAGE_LAZY_VALUE_H__
