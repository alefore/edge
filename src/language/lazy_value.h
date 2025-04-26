#ifndef __AFC_LANGUAGE_LAZY_VALUE_H__
#define __AFC_LANGUAGE_LAZY_VALUE_H__

#include <memory>
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
            std::move(factory))) {
    data_->lock([](const std::variant<Value, Factory>& data) {
      CHECK(std::get_if<Factory>(&data) != nullptr);
    });
  }
  LazyValue(const LazyValue&) = default;
  LazyValue(LazyValue&&) = default;
  LazyValue& operator=(const LazyValue&) = default;
  LazyValue& operator=(LazyValue&&) = default;

  const Value& get() const {
    return data_->lock([](std::variant<Value, Factory>& data) -> const Value& {
      if (Factory* factory = std::get_if<Factory>(&data); factory != nullptr)
        data = std::move(*factory)();
      return std::get<Value>(data);
    });
  }
};

template <typename Callable>
auto MakeLazyValue(Callable callable) {
  return LazyValue<std::invoke_result_t<Callable>>{callable};
}

template <typename Value>
LazyValue<Value> WrapAsLazyValue(Value value) {
  return MakeLazyValue(
      [value = std::move(value)] mutable -> Value { return std::move(value); });
}
}  // namespace afc::language
#endif  // __AFC_LANGUAGE_LAZY_VALUE_H__
