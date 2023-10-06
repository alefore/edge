#ifndef __AFC_MATH_NUMBERS_H__
#define __AFC_MATH_NUMBERS_H__

#include <memory>
#include <string>

#include "src/language/error/value_or_error.h"
#include "src/language/safe_types.h"

namespace afc::math::numbers {
struct OperationTree;
struct Number {
  language::NonNull<std::shared_ptr<const OperationTree>> value;

  Number& operator+=(Number rhs);
  Number& operator-=(Number rhs);
  Number& operator*=(Number rhs);
  Number& operator/=(Number rhs);
};

Number operator+(Number a, Number b);
Number operator-(Number a, Number b);
Number operator*(Number a, Number b);
Number operator/(Number a, Number b);
Number operator-(Number a);

afc::language::ValueOrError<std::wstring> ToString(const Number& number,
                                                   size_t decimal_digits);

Number FromInt(int64_t);
afc::language::ValueOrError<int32_t> ToInt32(const Number& number);
afc::language::ValueOrError<int64_t> ToInt(const Number& number);

afc::language::ValueOrError<double> ToDouble(const Number& number);
Number FromDouble(double);

Number FromSizeT(size_t);
afc::language::ValueOrError<size_t> ToSizeT(const Number& number);

afc::language::ValueOrError<bool> IsEqual(const Number& a, const Number& b,
                                          size_t precision);
afc::language::ValueOrError<bool> IsLessThan(const Number& a, const Number& b,
                                             size_t precision);
afc::language::ValueOrError<bool> IsLessThanOrEqual(const Number& a,
                                                    const Number& b,
                                                    size_t precision);

}  // namespace afc::math::numbers

#endif  // __AFC_MATH_NUMBERS_H__
