#ifndef __AFC_EDITOR_FUNCTION_TRAITS_H__
#define __AFC_EDITOR_FUNCTION_TRAITS_H__

// TODO(easy): Deduplicate with versions in vm/public/callbacks.h

template <typename T>
struct function_traits : public function_traits<decltype(&T::operator())> {};

template <typename ClassType, typename R, typename... Args>
struct function_traits<R (ClassType::*)(Args...) const> {
  using ReturnType = R;
  using ArgTuple = std::tuple<Args...>;
  static constexpr auto arity = sizeof...(Args);
};

template <typename R, typename... Args>
struct function_traits<R (&)(Args...)> {
  using ReturnType = R;
  using ArgTuple = std::tuple<Args...>;
  static constexpr auto arity = sizeof...(Args);
};

template <typename R, typename... Args>
struct function_traits<R (*)(Args...)> {
  using ReturnType = R;
  using ArgTuple = std::tuple<Args...>;
  static constexpr auto arity = sizeof...(Args);
};

template <typename R, typename... Args>
struct function_traits<R (*const)(Args...)> {
  using ReturnType = R;
  using ArgTuple = std::tuple<Args...>;
  static constexpr auto arity = sizeof...(Args);
};

#endif  // __AFC_EDITOR_FUNCTION_TRAITS_H__
