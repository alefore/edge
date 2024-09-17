#ifndef __AFC_EDITOR_LANGUAGE_CACHED_CALLABLE_H__
#define __AFC_EDITOR_LANGUAGE_CACHED_CALLABLE_H__

#include "src/concurrent/protected.h"
#include "src/language/once_only_function.h"

namespace afc::language {
// This class is thread-compatible. Ret is assumed to be a thread-compatible
// object.
template <typename Value>
class CachedSupplier {
  using Supplier = OnceOnlyFunction<Value()>;

  language::NonNull<
      std::shared_ptr<concurrent::Protected<std::variant<Supplier, Value>>>>
      data_;

 public:
  template <typename Callable>
  CachedSupplier(Callable&& callable)
      : data_(MakeNonNullShared<
              concurrent::Protected<std::variant<Supplier, Value>>>(
            OnceOnlyFunction<Value()>(std::forward<Callable>(callable)))) {}

  const Value& operator()() const {
    return *data_->lock([](std::variant<Supplier, Value>& data) {
      if (Supplier* supplier = std::get_if<Supplier>(&data);
          supplier != nullptr)
        data = std::invoke(std::move(*supplier));
      // Why not std::get<Value>(data)? Because the compiler gets confused and
      // warns us about returning a reference to a temporary value.
      return std::get_if<Value>(&data);
    });
  }
};

template <typename Callable>
auto MakeCachedSupplier(Callable&& callable) {
  return CachedSupplier<decltype(std::declval<Callable>()())>(
      std::forward<Callable>(callable));
}

}  // namespace afc::language
#endif  // __AFC_EDITOR_LANGUAGE_CACHED_CALLABLE_H__
