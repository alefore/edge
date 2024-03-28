#include "src/math/bigint.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <ranges>

#include "src/language/error/value_or_error.h"
#include "src/math/checked_operation.h"
#include "src/tests/tests.h"

using afc::language::Error;
using afc::language::IsError;
using afc::language::NewError;
using afc::language::ValueOrDie;
using afc::language::ValueOrError;
using afc::language::lazy_string::ColumnNumberDelta;
using afc::language::lazy_string::LazyString;

namespace afc::math::numbers {

const bool multiplication_tests_registration =
    tests::Register(L"types::CheckedMultiply::Multiplication", [] {
      auto test_int32 = [](std::wstring name, int32_t input1, int32_t input2,
                           int32_t expectation, bool expect_error = false) {
        return tests::Test{
            .name = name,
            .callback = [input1, input2, expectation, expect_error] mutable {
              auto result = CheckedMultiply<int32_t, int32_t>(input1, input2);
              if (expect_error) {
                CHECK(IsError(result));
              } else {
                int32_t result_value = ValueOrDie(std::move(result));
                CHECK_EQ(result_value, expectation)
                    << input1 << " * " << input2 << " == " << result_value;
              }
            }};
      };

      auto test_uint32 = [](std::wstring name, uint32_t input1, uint32_t input2,
                            uint32_t expectation, bool expect_error = false) {
        return tests::Test{
            .name = name,
            .callback = [input1, input2, expectation, expect_error] mutable {
              auto result = CheckedMultiply<uint32_t, uint32_t>(input1, input2);
              if (expect_error) {
                CHECK(IsError(result));
              } else {
                CHECK_EQ(ValueOrDie(std::move(result)), expectation);
              }
            }};
      };

      return std::vector<tests::Test>({
          test_int32(L"PositiveIntegers", 123, 456, 56088),
          test_int32(L"PositiveByNegative", 123, -456, -56088),
          test_int32(L"NegativeByNegative", -123, -456, 56088),
          test_int32(L"NegativeByPositive", -123, 456, -56088),
          test_int32(L"MinPositiveByNegative", 1, -1, -1),
          test_int32(L"MaxIntByMinusOne", std::numeric_limits<int32_t>::max(),
                     -1, -std::numeric_limits<int32_t>::max()),
          test_uint32(L"MaxUIntResult",
                      std::numeric_limits<uint32_t>::max() / 2, 2,
                      std::numeric_limits<uint32_t>::max() - 1),
          test_uint32(L"MaxUIntResultPlusOneOverflows",
                      std::numeric_limits<uint32_t>::max() / 2 + 1, 2, 0, true),
          test_uint32(L"ZeroMultiplication", 0, 123456789, 0),
          test_int32(L"ZeroByPositive", 0, 456, 0),
          test_int32(L"PositiveOverflow", std::numeric_limits<int32_t>::max(),
                     2, 0, true),
          test_int32(L"NegativeOverflow", std::numeric_limits<int32_t>::min(),
                     -1, 0, true),
          test_uint32(L"UIntOverflow", std::numeric_limits<uint32_t>::max(), 2,
                      0, true),
          test_int32(L"IntUnderflow", std::numeric_limits<int32_t>::min(), 2, 0,
                     true),
          test_int32(L"SpecialCaseMinByMinusOne",
                     std::numeric_limits<int32_t>::min(), -1, 0, true),
      });
    }());

BigInt::BigInt(std::vector<Digit> digits_input)
    : digits(std::move(digits_input)) {
  while (!digits.empty() && digits.back() == 0) digits.pop_back();
  for (Digit digit : digits) CHECK_LE(digit, 9UL);
}

namespace {
const bool constructors_tests_registration =
    tests::Register(L"numbers::BigInt::Constructors", [] {
      auto vector_constructor_test = [](std::vector<size_t> digits,
                                        std::wstring name,
                                        std::wstring expected) {
        return tests::Test{.name = name, .callback = [digits, expected] {
                             std::wstring value =
                                 afc::math::numbers::BigInt(digits).ToString();
                             CHECK(value == expected)
                                 << "Expected: " << expected
                                 << ", output: " << value;
                           }};
      };

      return std::vector<tests::Test>({
          tests::Test{.name = L"DefaultConstructor",
                      .callback =
                          [] {
                            afc::math::numbers::BigInt bigInt;
                            std::wstring value = bigInt.ToString();
                            LOG(INFO)
                                << "Default constructor output: " << value;
                            CHECK(value == L"0");
                          }},
          vector_constructor_test({1, 2, 3}, L"SimpleNumber", L"321"),
          vector_constructor_test({0, 1, 2, 3}, L"LeadingZeros", L"3210"),
          vector_constructor_test({1, 2, 3, 0, 0}, L"TrailingZeros", L"321"),
          vector_constructor_test({4, 9, 2, 2, 3, 2, 7, 2, 0, 3, 6, 8, 5, 4, 7},
                                  L"LargeNumber", L"745863027232294"),
          vector_constructor_test({}, L"EmptyVector", L"0"),
          vector_constructor_test({0, 0, 0, 0}, L"OnlyZeros", L"0"),
      });
    }());
}  // namespace

/* static */ ValueOrError<BigInt> BigInt::FromString(
    const std::wstring& input) {
  if (input.empty()) return NewError(LazyString{L"Input string is empty."});

  size_t start = input[0] == L'-' || input[0] == L'+' ? 1 : 0;

  std::vector<Digit> digits;
  for (size_t i = start; i < input.size(); ++i) {
    if (input[i] < L'0' || input[i] > L'9') {
      return NewError(LazyString{L"Invalid character found: "} +
                      LazyString{ColumnNumberDelta{1}, input[i]});
    }
    digits.insert(digits.begin(), input[i] - L'0');
  }

  if (digits.empty()) return NewError(LazyString{L"No digits found in input."});

  return BigInt(std::move(digits));
}

const bool from_string_tests_registration =
    tests::Register(L"numbers::BigInt::FromString", [] {
      auto test = [](std::wstring input,
                     std::optional<std::wstring> name = std::nullopt,
                     std::optional<std::wstring> expectation = std::nullopt) {
        return tests::Test{
            .name = name.value_or(input), .callback = [input, expectation] {
              std::wstring value =
                  ValueOrDie(BigInt::FromString(input)).ToString();
              LOG(INFO) << "Input: " << input << ", output: " << value;
              CHECK(value == expectation.value_or(input));
            }};
      };
      return std::vector<tests::Test>({
          test(L"1"),
          test(L"+1", {}, L"1"),
          test(L"123456789"),
          test(L"+123456789", {}, L"123456789"),
          test(L"0"),
          test(L"+0", {}, L"0"),
          test(L"00001234", L"LeadingZeros", L"1234"),
          test(L"999999999999999999999999999999999999", L"Large",
               L"999999999999999999999999999999999999"),
          test(std::wstring(100000, L'6'), L"VeryLarge"),
      });
    }());

bool BigInt::IsZero() const { return digits.empty(); }

bool BigInt::operator==(const BigInt& b) const { return digits == b.digits; }

bool BigInt::operator!=(const BigInt& b) const { return !(*this == b); }

bool BigInt::operator>(const BigInt& b) const {
  if (digits.size() != b.digits.size()) return digits.size() > b.digits.size();
  for (auto it = digits.rbegin(), it_b = b.digits.rbegin(); it != digits.rend();
       ++it, ++it_b) {
    CHECK(it_b != b.digits.rend());  // Silence -Wnull-dereference warning.
    if (*it != *it_b) return *it > *it_b;
  }
  return false;
}

bool BigInt::operator<(const BigInt& b) const { return b > *this; }
bool BigInt::operator>=(const BigInt& b) const {
  return *this > b || *this == b;
}
bool BigInt::operator<=(const BigInt& b) const { return b >= *this; }

BigInt BigInt::operator+(BigInt b) && {
  std::vector<Digit> result;

  const size_t max_size = std::max(digits.size(), b.digits.size());
  result.resize(max_size, 0);

  size_t carry = 0;
  for (size_t i = 0; i < max_size; ++i) {
    Digit a_digit = i < digits.size() ? digits[i] : 0;
    Digit b_digit = i < b.digits.size() ? b.digits[i] : 0;
    CHECK_LE(a_digit, 9UL);
    CHECK_LE(b_digit, 9UL);
    Digit sum = a_digit + b_digit + carry;
    carry = sum / 10;
    CHECK_LE(carry, 1ul);
    result[i] = sum % 10;
  }

  if (carry > 0) result.push_back(carry);
  return BigInt(std::move(result));
}

ValueOrError<BigInt> BigInt::operator-(BigInt b) && {
  if (*this < b) return NewError(LazyString{L"Subtraction would underflow."});

  std::vector<Digit> output_digits;
  int borrow = 0;
  for (size_t i = 0; i < digits.size(); ++i) {
    Digit a_digit = i < digits.size() ? digits[i] : 0;
    Digit b_digit = i < b.digits.size() ? b.digits[i] : 0;
    int diff = a_digit - b_digit - borrow;
    if (diff < 0) {
      diff += 10;
      CHECK_GE(diff, 0);
      borrow = 1;
    } else {
      borrow = 0;
    }
    output_digits.push_back(diff);
  }

  return BigInt(std::move(output_digits));
}

BigInt BigInt::operator*(const BigInt& b) const {
  std::vector<Digit> output_digits;
  output_digits.resize(digits.size() + b.digits.size(), 0);

  // Perform multiplication digit by digit:
  for (size_t i = 0; i < digits.size(); ++i) {
    for (size_t j = 0; j < b.digits.size(); ++j) {
      int product = digits[i] * b.digits[j];
      output_digits[i + j] += product;

      for (size_t k = i + j; output_digits[k] >= 10; ++k) {  // Handle carry.
        output_digits[k + 1] += output_digits[k] / 10;
        output_digits[k] %= 10;
      }
    }
  }
  return BigInt(std::move(output_digits));
}

BigInt& BigInt::operator++() {
  *this = std::move(*this) + BigInt::FromNumber(1);
  return *this;
}

language::ValueOrError<BigInt> operator/(BigInt numerator, BigInt denominator) {
  DECLARE_OR_RETURN(BigIntDivideOutput values,
                    Divide(std::move(numerator), std::move(denominator)));
  if (values.remainder != BigInt::FromNumber(0))
    return NewError(LazyString{L"Non-empty reminder: "} +
                    LazyString{values.remainder.ToString()});
  return values.quotient;
}

language::ValueOrError<BigIntDivideOutput> Divide(BigInt numerator,
                                                  BigInt denominator) {
  if (denominator.digits.empty())
    return NewError(LazyString{L"Division by zero."});

  BigInt quotient;
  BigInt current_dividend;

  for (size_t i = 0; i < numerator.digits.size(); ++i) {
    BigInt::Digit next = numerator.digits[numerator.digits.size() - 1 - i];
    CHECK_LE(next, 9ul);
    if (!current_dividend.digits.empty() || next != 0)
      current_dividend.digits.insert(current_dividend.digits.begin(), next);

    // Largest number such that denominator * x <= current_dividend:
    size_t x = 0;
    while (denominator * BigInt::FromNumber(x + 1) <= current_dividend) {
      ++x;
      CHECK_LE(x, 10UL);
    }

    if (x > 0)
      current_dividend =
          ValueOrDie(std::move(current_dividend) -
                     BigInt(denominator) * BigInt::FromNumber(x));
    CHECK_LE(x, 9ul);
    quotient.digits.insert(quotient.digits.begin(), x);
  }

  return BigIntDivideOutput{.quotient = BigInt(std::move(quotient.digits)),
                            .remainder = std::move(current_dividend)};
}

language::ValueOrError<BigInt> operator%(BigInt numerator, BigInt denominator) {
  DECLARE_OR_RETURN(BigIntDivideOutput values,
                    Divide(std::move(numerator), std::move(denominator)));
  return values.remainder;
}

/* static */ BigInt BigInt::Pow(BigInt base, BigInt exponent) {
  BigInt output = BigInt::FromNumber(1);
  while (exponent > BigInt()) {
    BigIntDivideOutput divide_result =
        ValueOrDie(Divide(std::move(exponent), BigInt::FromNumber(2)));
    if (!divide_result.remainder.IsZero()) output = std::move(output) * base;
    base = base * base;
    exponent = std::move(divide_result.quotient);
  }
  return output;
}

namespace {
const bool pow_tests_registration =
    tests::Register(L"numbers::BigInt::Pow", [] {
      auto test = [](std::wstring name, BigInt base, BigInt exponent,
                     BigInt expectation) {
        return tests::Test{
            .name = name, .callback = [base, exponent, expectation] mutable {
              BigInt output = BigInt::Pow(std::move(base), std::move(exponent));
              CHECK(output == expectation);
            }};
      };

      return std::vector<tests::Test>({
          // Basic Functionality Tests
          test(L"SmallNumbers", BigInt::FromNumber(2), BigInt::FromNumber(3),
               BigInt::FromNumber(8)),
          test(L"BaseOne", BigInt::FromNumber(1), BigInt::FromNumber(5),
               BigInt::FromNumber(1)),
          test(L"ExponentZero", BigInt::FromNumber(5), BigInt::FromNumber(0),
               BigInt::FromNumber(1)),
          test(L"ZeroPowerOfPositive", BigInt::FromNumber(0),
               BigInt::FromNumber(4), BigInt::FromNumber(0)),
          test(L"TenToTheFifty", BigInt::FromNumber(10), BigInt::FromNumber(50),
               ValueOrDie(BigInt::FromString(
                   L"100000000000000000000000000000000000000000000000000"))),

          test(L"LargeBaseSmallExponent",
               ValueOrDie(BigInt::FromString(L"123456789")),
               BigInt::FromNumber(2),
               ValueOrDie(BigInt::FromString(L"15241578750190521"))),

          test(L"ZeroPowerZero", BigInt::FromNumber(0), BigInt::FromNumber(0),
               BigInt::FromNumber(1)),

          test(L"PowerOfTwoExponent", BigInt::FromNumber(2),
               BigInt::FromNumber(10), BigInt::FromNumber(1024)),
          test(L"ConsecutivePowers", BigInt::FromNumber(3),
               BigInt::FromNumber(3), BigInt::FromNumber(27)),
      });
    }());
}  // namespace

BigInt BigInt::GreatestCommonDivisor(const BigInt& other) const {
  BigInt zero;
  BigInt a = *this;
  BigInt b = other;

  while (b != zero) {
    // TODO(2024-03-14): Get rid of ValueOrDie.
    a = ValueOrDie(std::move(a) % BigInt(b));
    std::swap(a, b);
  }
  return a;
}

ValueOrError<int32_t> BigInt::ToInt32() const { return ToNumber<int32_t>(); }

ValueOrError<int64_t> BigInt::ToInt64() const { return ToInt64(true); }

ValueOrError<int64_t> BigInt::ToInt64(bool positive) const {
  return ToNumber<int64_t>(positive);
}

language::ValueOrError<size_t> BigInt::ToSizeT() const {
  return ToNumber<size_t>();
}

language::ValueOrError<double> BigInt::ToDouble() const {
  double value = 0;
  for (double digit : digits | std::views::reverse) {
    value *= 10;
    value += digit;
  }
  return value;
}

namespace {
const bool greater_than_tests_registration =
    tests::Register(L"numbers::BigInt::GreaterThan", [] {
      auto test = [](std::wstring name, int input1, int input2,
                     bool expectation) {
        return tests::Test{
            .name = name, .callback = [input1, input2, expectation] {
              BigInt int_1 = BigInt::FromNumber(input1);
              BigInt int_2 = BigInt::FromNumber(input2);
              bool result = int_1 > int_2;
              CHECK(result == expectation)
                  << "Unexpected comparison result: " << input1 << " > "
                  << input2 << " yields " << (result ? "true" : "false")
                  << ", expected: " << (expectation ? "true" : "false");
            }};
      };

      return std::vector<tests::Test>({
          test(L"SimpleGreaterThan", 123, 45, true),
          test(L"PositiveGreaterThanZero", 123, 0, true),
          test(L"ZeroNotGreaterThanPositive", 0, 123, false),
          test(L"EqualNumbers", 100, 100, false),
          test(L"LargeNumbers", 1000000001, 1000000000, true),
          test(L"DifferentLengthsPositive", 12345, 123, true),
          test(L"CloseValues", 1001, 1000, true),
      });
    }());

const bool addition_tests_registration =
    tests::Register(L"numbers::BigInt::Addition", [] {
      auto test = [](std::wstring name, BigInt input1, BigInt input2,
                     std::wstring expectation) {
        return tests::Test{
            .name = name, .callback = [input1, input2, expectation] mutable {
              std::wstring output =
                  (std::move(input1) + std::move(input2)).ToString();
              CHECK(output == expectation);
            }};
      };

      return std::vector<tests::Test>({
          test(L"Positive", BigInt::FromNumber(123), BigInt::FromNumber(456),
               L"579"),
          test(L"WithZeroFirst", BigInt::FromNumber(0), BigInt::FromNumber(456),
               L"456"),
          test(L"WithZeroSecond", BigInt::FromNumber(123),
               BigInt::FromNumber(0), L"123"),
          test(L"LargeNumbers",
               ValueOrDie(BigInt::FromString(L"999999999999999999")),
               ValueOrDie(BigInt::FromString(L"111111111111111111")),
               L"1111111111111111110"),
          test(L"VeryLargeNumbers",
               ValueOrDie(
                   BigInt::FromString(L"999999999999999999999999999999999999")),
               BigInt::FromNumber(1), L"1000000000000000000000000000000000000"),
          test(L"EdgeCaseLargeSum",
               ValueOrDie(BigInt::FromString(L"18446744073709551615")),
               BigInt::FromNumber(1), L"18446744073709551616"),
      });
    }());

const bool subtraction_tests_registration =
    tests::Register(L"numbers::BigInt::Subtraction", [] {
      auto test = [](std::wstring name, BigInt input1, BigInt input2,
                     std::wstring expectation) {
        return tests::Test{
            .name = name, .callback = [input1, input2, expectation] mutable {
              std::wstring output =
                  ValueOrDie(std::move(input1) - std::move(input2)).ToString();
              CHECK(output == expectation);
            }};
      };

      return std::vector<tests::Test>({
          test(L"SimpleSubtraction", BigInt::FromNumber(456),
               BigInt::FromNumber(123), L"333"),
          test(L"SubtractionBorrowing", BigInt::FromNumber(500),
               BigInt::FromNumber(256), L"244"),
          test(L"SubtractionEquals", BigInt::FromNumber(123),
               BigInt::FromNumber(123), L"0"),
          test(L"SubtractZero", BigInt::FromNumber(123), BigInt::FromNumber(0),
               L"123"),
          test(L"LargeNumbers",
               ValueOrDie(BigInt::FromString(L"10000000000000000000")),
               ValueOrDie(BigInt::FromString(L"1")), L"9999999999999999999"),
      });
    }());

const bool multiplication_tests_registration =
    tests::Register(L"numbers::BigInt::Multiplication", [] {
      auto test = [](std::wstring name, BigInt input1, BigInt input2,
                     std::wstring expectation) {
        return tests::Test{
            .name = name, .callback = [input1, input2, expectation] {
              std::wstring output = (input1 * input2).ToString();
              CHECK(output == expectation)
                  << input1.ToString() << " * " << input2.ToString()
                  << " yields " << output << ", but expected: " << expectation;
            }};
      };
      return std::vector<tests::Test>({
          test(L"SimpleMultiplication", BigInt::FromNumber(2),
               BigInt::FromNumber(3), L"6"),
          test(L"MultiplicationByZero", BigInt::FromNumber(12345),
               BigInt::FromNumber(0), L"0"),
          test(L"SingleDigitMultiplicationRequiringCarry",
               BigInt::FromNumber(9), BigInt::FromNumber(9), L"81"),
          test(L"MultipleDigitsWithCarry", BigInt::FromNumber(15),
               BigInt::FromNumber(27), L"405"),
          test(L"ZeroMultiplicationLargeNumber", BigInt::FromNumber(0),
               ValueOrDie(BigInt::FromString(std::wstring(1000, L'9'))), L"0"),
          test(L"LargeNumberMultiplication",
               ValueOrDie(BigInt::FromString(std::wstring(50, L'9'))),
               ValueOrDie(BigInt::FromString(L"1" + std::wstring(100, L'0'))),
               std::wstring(50, L'9') + std::wstring(100, L'0')),
          test(L"DistributiveProperty", ValueOrDie(BigInt::FromString(L"5")),
               ValueOrDie(BigInt::FromString(L"2")) +
                   ValueOrDie(BigInt::FromString(L"3")),
               L"25"),

      });
    }());

const bool division_tests_registration =
    tests::Register(L"numbers::BigInt::Division", [] {
      auto test = [](std::wstring name, int numerator, int denominator,
                     std::optional<std::wstring> expected_outcome) {
        return tests::Test{
            .name = name,
            .callback = [numerator, denominator, expected_outcome] {
              ValueOrError<BigInt> result = BigInt::FromNumber(numerator) /
                                            BigInt::FromNumber(denominator);
              if (expected_outcome.has_value()) {
                std::wstring output = ValueOrDie(std::move(result)).ToString();
                CHECK(output == expected_outcome.value());
              } else {
                CHECK(IsError(result));
              }
            }};
      };

      return std::vector<tests::Test>({
          test(L"SimpleDivision", 4, 2, L"2"),
          test(L"DivisionByOne", 123, 1, L"123"),
          test(L"DivisionByItself", 123, 123, L"1"),
          test(L"ZeroDivisionByNonZero", 0, 123, L"0"),
          test(L"DivisionByZero", 123, 0, std::nullopt),
          test(L"NonPerfectDivision", 3, 2, std::nullopt),
          test(L"LargeNumbersDivision", 100000, 1000, L"100"),
      });
    }());

const bool gcd_tests_registration =
    tests::Register(L"numbers::BigInt::GreatestCommonDivisor", [] {
      auto test = [](std::wstring name, int input1, int input2,
                     int expectation) {
        return tests::Test{
            .name = name, .callback = [input1, input2, expectation] {
              BigInt result = BigInt::FromNumber(input1).GreatestCommonDivisor(
                  BigInt::FromNumber(input2));
              CHECK(result == BigInt::FromNumber(expectation))
                  << "Unexpected GCD result for: " << input1 << " and "
                  << input2 << " yields " << result.ToString()
                  << ", expected: " << expectation;
            }};
      };

      return std::vector<tests::Test>({
          test(L"PositiveNumbers", 48, 18, 6),
          test(L"OneZeroValue", 0, 123, 123),
          test(L"BothZeroValues", 0, 0, 0),
          test(L"OneValueIsOne", 13, 1, 1),
          test(L"PrimeNumbers", 17, 19, 1),
          test(L"CompositeNumbersCommonDivisors", 54, 24, 6),
          test(L"IdenticalNumbers", 100, 100, 100),
          test(L"LargeNumbers", 1234567890, 987654321, 9),
          test(L"PrimeAndOne", 13, 1, 1),
          test(L"LargeIdenticalNumbers", 1000000000, 1000000000, 1000000000),
      });
    }());

}  // namespace
}  // namespace afc::math::numbers
