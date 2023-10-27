#ifndef __AFC_LANGUAGE_GC_UTIL_H__
#define __AFC_LANGUAGE_GC_UTIL_H__

#include <memory>
#include <optional>
#include <tuple>
#include <vector>

#include "src/language/gc.h"
#include "src/language/safe_types.h"

namespace afc::language::gc {

template <typename T>
struct is_void : std::false_type {};

template <>
struct is_void<void> : std::true_type {};

template <typename T>
struct IsGcPtr : std::false_type {};

template <typename T>
struct IsGcPtr<Ptr<T>> : std::true_type {};

template <typename Func, typename... BoundArgs>
class BindFrontImpl {
  struct ConstructorAccessTag {};

  Func func_;
  std::tuple<std::decay_t<BoundArgs>...> bound_args_;

 public:
  static gc::Root<BindFrontImpl<Func, BoundArgs...>> New(Pool& pool,
                                                         Func&& func,
                                                         BoundArgs&&... args) {
    return pool.NewRoot(MakeNonNullUnique<BindFrontImpl>(
        ConstructorAccessTag(), std::forward<Func>(func),
        std::forward<BoundArgs>(args)...));
  }

  BindFrontImpl(ConstructorAccessTag, Func&& func, BoundArgs&&... args)
      : func_(std::move(func)), bound_args_(std::forward<BoundArgs>(args)...) {}

  template <typename... Args>
  auto operator()(Args&&... args) {
    return invoke(std::index_sequence_for<BoundArgs...>{},
                  std::forward<Args>(args)...);
  }

  std::vector<NonNull<std::shared_ptr<gc::ObjectMetadata>>> Expand() const {
    std::vector<NonNull<std::shared_ptr<gc::ObjectMetadata>>> output;
    PtrExpandHelper<0>(output, bound_args_);
    return output;
  }

 private:
  template <std::size_t... Is, typename... Args>
  auto invoke(std::index_sequence<Is...>, Args&&... args) {
    WrapWithLocksHelper helper_;

    // Create a tuple of optional values:
    auto transformed_args =
        std::make_tuple(helper_(std::get<Is>(bound_args_))...);

    using ReturnType = decltype(std::declval<Func>()(
        std::declval<decltype(helper_(std::declval<BoundArgs>()))>().value()...,
        std::declval<Args>()...));

    // Check if any optional value is nullopt:
    if ((... || (std::get<Is>(transformed_args) == std::nullopt))) {
      if constexpr (is_void<ReturnType>::value)
        return;
      else
        return std::optional<ReturnType>();
    }

    // Call the function if all checks passed:
    if constexpr (is_void<ReturnType>::value) {
      func_(std::get<Is>(transformed_args).value()...,
            std::forward<Args>(args)...);
      return;
    } else
      return std::make_optional(func_(std::get<Is>(transformed_args).value()...,
                                      std::forward<Args>(args)...));
  }

  struct WrapWithLocksHelper {
    template <typename T>
    std::optional<T> operator()(const T& t) const {
      return t;
    }

    template <typename T>
    std::optional<gc::Root<T>> operator()(const gc::WeakPtr<T>& value) const {
      return value.Lock();
    }

    template <typename T>
    auto operator()(gc::Root<T>) const {
      static_assert(always_false_v<T>,
                    "Binding of gc::Root<T> is not allowed!");
    }

   private:
    template <typename...>
    inline constexpr static bool always_false_v = false;
  };

  template <size_t index, typename... Args>
  static void PtrExpandHelper(
      std::vector<NonNull<std::shared_ptr<ObjectMetadata>>>& output,
      const std::tuple<Args...>& tup) {
    if constexpr (index < sizeof...(Args)) {
      if constexpr (IsGcPtr<
                        std::decay_t<decltype(std::get<index>(tup))>>::value)
        output.push_back(std::get<index>(tup).object_metadata());
      PtrExpandHelper<index + 1>(output, tup);
    }
  }
};

template <typename Func, typename... Args>
auto BindFront(Pool& pool, Func&& func, Args&&... args) {
  return BindFrontImpl<Func, Args...>::New(pool, std::forward<Func>(func),
                                           std::forward<Args>(args)...);
}

template <typename Callback>
auto LockCallback(gc::Ptr<Callback> callback) {
  return [root = callback.ToRoot()] { root.ptr().value()(); };
}

template <typename Value>
struct ValueWithFixedDependencies {
  Value value;

  std::vector<NonNull<std::shared_ptr<gc::ObjectMetadata>>> dependencies;

  std::vector<NonNull<std::shared_ptr<gc::ObjectMetadata>>> Expand() const {
    return dependencies;
  }
};
}  // namespace afc::language::gc
#endif  // __AFC_LANGUAGE_GC_UTIL_H__
