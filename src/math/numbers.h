#ifndef __AFC_MATH_NUMBERS_H__
#define __AFC_MATH_NUMBERS_H__

#include <memory>
#include <string>

#include "src/language/error/value_or_error.h"
#include "src/language/safe_types.h"
#include "src/math/bigint.h"

namespace afc::math::numbers {
class Number {
  bool positive_;
  BigInt numerator_;
  NonZeroBigInt denominator_;

 public:
  Number(bool positive, BigInt numerator, NonZeroBigInt denominator)
      : positive_(positive),
        numerator_(std::move(numerator)),
        denominator_(std::move(denominator)) {}

  Number operator+(Number other) &&;
  Number operator-(Number other) &&;
  Number operator*(Number other) &&;
  language::ValueOrError<Number> operator/(Number other) &&;

  Number Negate() &&;
  language::ValueOrError<Number> Reciprocal() &&;

  void Optimize();
  std::wstring ToString(size_t maximum_decimal_digits) const;

  Number& operator+=(Number rhs);
  Number& operator-=(Number rhs);
  Number& operator*=(Number rhs);
  Number& operator/=(Number rhs);

  bool operator==(const Number& other) const;
  bool operator>(const Number& other) const;
  bool operator<(const Number& other) const;
  bool operator>=(const Number& other) const;
  bool operator<=(const Number& other) const;

  static Number FromBigInt(BigInt);
  static Number FromInt64(int64_t);
  static Number FromSizeT(size_t);
  static Number FromDouble(double);

  language::ValueOrError<int32_t> ToInt32() const;
  language::ValueOrError<int64_t> ToInt64() const;
  language::ValueOrError<size_t> ToSizeT() const;
  language::ValueOrError<double> ToDouble() const;

  Number Pow(BigInt exponent) &&;
};

}  // namespace afc::math::numbers

#endif  // __AFC_MATH_NUMBERS_H__
