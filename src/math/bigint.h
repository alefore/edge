#ifndef __AFC_MATH_BIGINT_H__
#define __AFC_MATH_BIGINT_H__

#include <ranges>
#include <string>
#include <vector>

#include "src/language/error/value_or_error.h"
#include "src/math/checked_operation.h"

namespace afc::math::numbers {

class BigIntDivideOutput;
class BigInt;
class NonZeroBigInt;

BigIntDivideOutput Divide(BigInt numerator, NonZeroBigInt denominator);

class BigInt {
 private:
  using Digit = size_t;
  std::vector<Digit> digits;  // Element 0 is the least significant digit.

 public:
  BigInt() = default;
  BigInt(std::vector<Digit> digits_input);
  BigInt& operator=(const BigInt&) = default;

  static language::ValueOrError<BigInt> FromString(const std::wstring& input);

  std::wstring ToString() const {
    if (digits.empty()) return L"0";

    std::wstring result;
    for (auto it = digits.rbegin(); it != digits.rend(); ++it)
      result += static_cast<wchar_t>(L'0' + *it);
    return result;
  }

  template <typename NumberType>
  static BigInt FromNumber(NumberType value) {
    std::vector<Digit> digits;
    do {
      digits.push_back(value % 10);
      value /= 10;
    } while (value != 0);
    return BigInt(std::move(digits));
  }

  bool IsZero() const;

  bool operator==(const BigInt& b) const;
  bool operator!=(const BigInt& b) const;
  bool operator>(const BigInt& b) const;
  bool operator<(const BigInt& b) const;
  bool operator>=(const BigInt& b) const;
  bool operator<=(const BigInt& b) const;

  BigInt operator+(BigInt b) &&;
  language::ValueOrError<BigInt> operator-(BigInt b) &&;
  BigInt operator*(const BigInt& b) const;
  BigInt& operator++();

  friend BigIntDivideOutput Divide(BigInt numerator, NonZeroBigInt denominator);

  BigInt Pow(BigInt exponent) &&;
  BigInt GreatestCommonDivisor(const BigInt& other) const;

  language::ValueOrError<int32_t> ToInt32() const;
  language::ValueOrError<int64_t> ToInt64() const;
  language::ValueOrError<int64_t> ToInt64(bool positive) const;
  language::ValueOrError<size_t> ToSizeT() const;
  language::ValueOrError<double> ToDouble() const;

 private:
  template <typename OutputType>
  language::ValueOrError<OutputType> ToNumber() const {
    return ToNumber<OutputType>(true);
  }

  template <typename OutputType>
  language::ValueOrError<OutputType> ToNumber(bool positive) const {
    OutputType value = 0;
    for (OutputType digit : digits | std::views::reverse) {
      ASSIGN_OR_RETURN(value, (CheckedMultiply(value, 10)));
      if (!positive) ASSIGN_OR_RETURN(digit, CheckedMultiply(digit, -1));
      ASSIGN_OR_RETURN(value, CheckedAdd(value, digit));
    }
    return value;
  }
};

struct BigIntDivideOutput {
  BigInt quotient;
  BigInt remainder;
};

language::ValueOrError<BigIntDivideOutput> Divide(BigInt numerator,
                                                  BigInt denominator);

language::ValueOrError<BigInt> operator%(BigInt numerator, BigInt denominator);
language::ValueOrError<BigInt> operator/(BigInt numerator, BigInt denominator);

class NonZeroBigInt {
  BigInt value_;  // Non-const to enable move-construction.

 public:
  static language::ValueOrError<NonZeroBigInt> New(BigInt value);
  const BigInt& value() const;

  NonZeroBigInt(const NonZeroBigInt&) = default;
  NonZeroBigInt(NonZeroBigInt&&) = default;
  NonZeroBigInt& operator=(const NonZeroBigInt&) = default;
  NonZeroBigInt& operator=(NonZeroBigInt&&) = default;

  NonZeroBigInt operator*(const NonZeroBigInt& b) const;

  NonZeroBigInt Pow(BigInt exponent) &&;

 private:
  NonZeroBigInt(BigInt validated_value);
};

}  // namespace afc::math::numbers

#endif  // __AFC_MATH_BIGINT_H__
