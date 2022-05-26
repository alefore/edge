// Defines a `function_traits` templated type with a few specializations.
//
// Given a type of a callable, defines these symbols:
//
// `ReturnType`: The type that the callable returns.
// `ArgType`: A typle with the types of all the arguments.
// `arity`: A constexpr with the number of arguments expected.
#ifndef __AFC_EDITOR_FUNCTION_TRAITS_H__
#define __AFC_EDITOR_FUNCTION_TRAITS_H__

namespace afc::language {
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
}  // namespace afc::language

#endif  // __AFC_EDITOR_FUNCTION_TRAITS_H__
