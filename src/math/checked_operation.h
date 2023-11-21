#ifndef __AFC_MATH_CHECKED_OPERATION__
#define __AFC_MATH_CHECKED_OPERATION__

#include "src/language/error/value_or_error.h"

namespace afc::math::numbers {
template <typename A, typename B>
language::ValueOrError<A> CheckedAdd(A a, B b) {
  if (b >= B() ? std::numeric_limits<A>::max() - b < a
               : std::numeric_limits<A>::min() - b > a)
    return language::Error(
        L"Overflow: the resulting number can't be represented.");
  return a + b;
}

template <typename A, typename B>
language::ValueOrError<A> CheckedMultiply(A a, B b) {
  static_assert(std::is_integral<A>::value,
                "CheckedMultiply supports only integral types.");
  static_assert(std::is_integral<B>::value,
                "CheckedMultiply supports only integral types.");
  if (a > 0 && b > 0) {
    if (a > std::numeric_limits<A>::max() / b)
      return language::Error(
          L"Overflow: the resulting number can't be represented.");
  } else if (a > 0 && b < 0) {
    if (a > std::numeric_limits<A>::min() / b)
      return language::Error(
          L"Underflow: the resulting number can't be represented.");
  } else if (a < 0 && b > 0) {
    if (a < std::numeric_limits<A>::min() / b)
      return language::Error(
          L"Underflow: the resulting number can't be represented.");
  } else if (a < 0 && b < 0) {
    if (a < std::numeric_limits<A>::max() / b)
      return language::Error(
          L"Overflow: the resulting number can't be represented.");
  }
  return a * b;
}
}  // namespace afc::math::numbers
#endif
