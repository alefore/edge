#ifndef __AFC_LANGUAGE_ONCE_ONLY_FUNCTION_H__
#define __AFC_LANGUAGE_ONCE_ONLY_FUNCTION_H__

#include <functional>  // For std::move_only_function
#include <stdexcept>
#include <utility>

namespace afc::language {
template <typename Signature>
class OnceOnlyFunction;

template <typename Ret, typename... Args>
class OnceOnlyFunction<Ret(Args...)> {
  std::move_only_function<Ret(Args...)> func_;

  template <typename Other>
  friend class OnceOnlyFunction;

 public:
  template <typename Callable>
  OnceOnlyFunction(Callable&& callable)
      : func_(std::forward<Callable>(callable)) {
    CHECK(func_ != nullptr);
  }

  template <typename Other>
  OnceOnlyFunction(OnceOnlyFunction<Other>&& other)
      : OnceOnlyFunction(std::move(other.func_)) {}

  OnceOnlyFunction(const OnceOnlyFunction&) = delete;
  OnceOnlyFunction& operator=(const OnceOnlyFunction&) = delete;
  OnceOnlyFunction& operator=(OnceOnlyFunction&& other) noexcept = default;

  Ret operator()(Args... args) && { return func_(std::forward<Args>(args)...); }
};
}  // namespace afc::language
#endif  // __AFC_LANGUAGE_ONCE_ONLY_FUNCTION_H__
