#ifndef __AFC_MATH_BIGINT_H__
#define __AFC_MATH_BIGINT_H__

#include <string>
#include <vector>

#include "src/language/error/value_or_error.h"

namespace afc::math::numbers {

class BigInt;

bool operator==(const BigInt& a, const BigInt& b);
bool operator>(const BigInt& a, const BigInt& b);
bool operator<(const BigInt& a, const BigInt& b);
bool operator>=(const BigInt& a, const BigInt& b);
bool operator<=(const BigInt& a, const BigInt& b);

BigInt operator+(BigInt a, BigInt b);
BigInt operator-(BigInt a, BigInt b);
BigInt operator*(BigInt& a, BigInt& b);
language::ValueOrError<BigInt> operator/(BigInt numerator, BigInt denominator);

std::wstring ToString(const BigInt& a);
BigInt NewBigInt(double value);
BigInt NewBigInt(int64_t value);

language::ValueOrError<int64_t> ToInt64(const BigInt& a);
language::ValueOrError<double> ToDouble(const BigInt& a);
}  // namespace afc::math::numbers

#endif  // __AFC_MATH_BIGINT_H__
