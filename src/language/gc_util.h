#ifndef __AFC_LANGUAGE_GC_UTIL_H__
#define __AFC_LANGUAGE_GC_UTIL_H__

namespace afc::language::gc {

template <typename T>
struct is_void : std::false_type {};

template <>
struct is_void<void> : std::true_type {};

template <typename Func, typename... BoundArgs>
class BindFrontWithWeakPtrImpl {
 public:
  BindFrontWithWeakPtrImpl(Func&& func, BoundArgs&&... args)
      : func_(std::move(func)), bound_args_(std::forward<BoundArgs>(args)...) {}

  template <typename... Args>
  auto operator()(Args&&... args) {
    return invoke(std::index_sequence_for<BoundArgs...>{},
                  std::forward<Args>(args)...);
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

  Func func_;
  std::tuple<BoundArgs...> bound_args_;
};

template <typename Func, typename... Args>
auto BindFrontWithWeakPtr(Func&& func, Args&&... args) {
  return BindFrontWithWeakPtrImpl<Func, Args...>(std::forward<Func>(func),
                                                 std::forward<Args>(args)...);
}
}  // namespace afc::language::gc
#endif  // __AFC_LANGUAGE_GC_UTIL_H__
