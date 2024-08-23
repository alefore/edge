#ifndef __AFC_MATH_BIGINT_H__
#define __AFC_MATH_BIGINT_H__

#include <ranges>
#include <string>
#include <vector>

#include "src/language/error/value_or_error.h"
#include "src/language/ghost_type_class.h"
#include "src/math/checked_operation.h"

namespace afc::math::numbers {

class BigIntDivideOutput;
class BigInt;
class NonZeroBigInt;

// Forward declaration to enable `friend` declaration.
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

  std::wstring ToString() const;
  language::lazy_string::LazyString ToLazyString() const;

  template <typename NumberType>
  static BigInt FromNumber(NumberType value) {
    CHECK_GE(value, 0);
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

  BigInt& operator+=(BigInt rhs);
  BigInt& operator*=(BigInt rhs);

  friend BigIntDivideOutput Divide(BigInt numerator, NonZeroBigInt denominator);

  BigInt Pow(BigInt exponent) &&;

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

std::ostream& operator<<(std::ostream& os, const BigInt& p);

struct BigIntDivideOutput {
  BigInt quotient;
  BigInt remainder;
};

// Returns an error if `denominator` is zero.
language::ValueOrError<BigIntDivideOutput> Divide(BigInt numerator,
                                                  BigInt denominator);

language::ValueOrError<BigInt> operator%(BigInt numerator, BigInt denominator);
language::ValueOrError<BigInt> operator/(BigInt numerator, BigInt denominator);

struct NonZeroBigIntValidator {
  static language::PossibleError Validate(const BigInt& input);
};

class NonZeroBigInt : public language::GhostType<NonZeroBigInt, BigInt,
                                                 NonZeroBigIntValidator> {
 public:
  template <int N>
  static NonZeroBigInt Constant() {
    static_assert(N > 0, "N must be greater than 0.");
    return NonZeroBigInt(BigInt::FromNumber(N));
  }

  NonZeroBigInt operator+(BigInt b) &&;
  NonZeroBigInt operator*(const NonZeroBigInt& b) const;

  NonZeroBigInt& operator+=(NonZeroBigInt rhs);
  NonZeroBigInt& operator*=(NonZeroBigInt rhs);

  NonZeroBigInt Pow(BigInt exponent) &&;

  NonZeroBigInt GreatestCommonDivisor(const NonZeroBigInt& other) const;
};

bool operator==(const NonZeroBigInt& a, const NonZeroBigInt& b);
BigInt operator%(BigInt numerator, NonZeroBigInt denominator);
std::ostream& operator<<(std::ostream& os, const NonZeroBigInt& p);

}  // namespace afc::math::numbers

#endif  // __AFC_MATH_BIGINT_H__
