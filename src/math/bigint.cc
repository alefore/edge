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
using ::operator<<;

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
  size_t start = input[0] == L'+' ? 1 : 0;
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

namespace {
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
      auto test_error = [](std::wstring input) {
        return tests::Test{
            .name = L"Error" + input, .callback = [input] {
              ValueOrError<BigInt> value = BigInt::FromString(input);
              CHECK(IsError(value)) << "Expected error but received value: "
                                    << ValueOrDie(std::move(value)).ToString();
              LOG(INFO) << "Received expected error: "
                        << std::get<Error>(value);
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
          // TODO(trivial): Make this test pass?
          // test(L"123.0", {}, L"123"),
          test_error(L"-1"),
          test_error(L"123x9"),
          test_error(L""),
          test_error(L"1.5"),
      });
    }());
}  // namespace

bool BigInt::IsZero() const { return digits.empty(); }

namespace {
const bool is_zero_tests_registration = tests::Register(
    L"numbers::BigInt::IsZero",
    std::vector<tests::Test>({
        {.name = L"Zero", .callback = [] { CHECK(BigInt().IsZero()); }},
        {.name = L"One",
         .callback = [] { CHECK(!BigInt::FromNumber(1).IsZero()); }},
        {.name = L"OneFromStringLeadingZeros",
         .callback =
             [] { CHECK(!ValueOrDie(BigInt::FromString(L"00001")).IsZero()); }},
        {.name = L"ZeroFromString",
         .callback = [] { CHECK(BigInt::FromNumber(0).IsZero()); }},
        {.name = L"ZeroFromStringLeadingZeros",
         .callback =
             [] { CHECK(ValueOrDie(BigInt::FromString(L"0000")).IsZero()); }},
        {.name = L"ZeroFromStringPlus",
         .callback =
             [] { CHECK(ValueOrDie(BigInt::FromString(L"+0")).IsZero()); }},
        {.name = L"ZeroFromStringLarge",
         .callback =
             [] {
               CHECK(
                   !ValueOrDie(BigInt::FromString(L"9230789434958349578345987"))
                        .IsZero());
             }},

    }));
}  // namespace

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
      });
    }());

const bool tests_order_registration = tests::Register(
    L"numbers::BigInt::Order",
    std::vector<tests::Test>(
        {{.name = L"Combinations", .callback = [] {
            std::vector<BigInt> values = {
                BigInt::FromNumber(0),    BigInt::FromNumber(1),
                BigInt::FromNumber(2),    BigInt::FromNumber(10),
                BigInt::FromNumber(1024),
            };
            for (size_t i = 0; i < values.size(); i++) {
              LOG(INFO) << "i := " << i;
              CHECK_EQ(values[i], values[i]);
              CHECK(!(values[i] != values[i]));
              CHECK_LE(values[i], values[i]);
              CHECK_GE(values[i], values[i]);
              for (size_t j = i + 1; j < values.size(); j++) {
                LOG(INFO) << "i := " << i << ", j := " << j;
                CHECK_NE(values[i], values[j]);
                CHECK_NE(values[j], values[i]);
                CHECK(!(values[i] == values[j]));
                CHECK(!(values[j] == values[i]));
                CHECK_LE(values[i], values[j]);
                CHECK_LT(values[i], values[j]);
                CHECK_GE(values[j], values[i]);
                CHECK_GT(values[j], values[i]);
              }
            }
          }}}));
}  // namespace

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

namespace {
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
          test(L"Normal", BigInt::FromNumber(123), BigInt::FromNumber(456),
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
}  // namespace

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

namespace {
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
          {.name = L"UnderflowZero",
           .callback =
               [] {
                 CHECK(IsError(BigInt::FromNumber(0) - BigInt::FromNumber(1)));
               }},
          {.name = L"UnderflowNormal",
           .callback =
               [] {
                 CHECK(IsError(BigInt::FromNumber(123) -
                               BigInt::FromNumber(456)));
               }},
      });
    }());
}  // namespace

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

namespace {
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
}  // namespace

BigInt& BigInt::operator++() {
  *this = std::move(*this) + BigInt::FromNumber(1);
  return *this;
}

namespace {
const bool increment_tests_registration = tests::Register(
    L"numbers::BigInt::operator++", std::invoke([] {
      auto test = [](std::wstring name, int32_t input) {
        return tests::Test{.name = std::move(name), .callback = [input] {
                             BigInt num = BigInt::FromNumber(input);
                             ++num;
                             CHECK_EQ(num, BigInt::FromNumber(input + 1));
                           }};
      };
      return std::vector<tests::Test>{
          test(L"ZeroIncrement", 0),
          test(L"SingleDigitIncrement", 5),
          test(L"Boundary", 99),
          test(L"LargeNumberIncrement", 87654),
          {.name = L"RepetitiveIncrement",
           .callback =
               [] {
                 BigInt number = BigInt::FromNumber(100);
                 for (size_t i = 0; i < 100; ++i) {
                   CHECK_EQ(number, BigInt::FromNumber(100 + i));
                   ++number;
                 }
                 CHECK_EQ(number, BigInt::FromNumber(200));
               }},
      };
    }));
}  // namespace

language::ValueOrError<BigInt> operator/(BigInt numerator, BigInt denominator) {
  DECLARE_OR_RETURN(BigIntDivideOutput values,
                    Divide(std::move(numerator), std::move(denominator)));
  if (values.remainder != BigInt::FromNumber(0))
    return NewError(LazyString{L"Non-empty reminder: "} +
                    LazyString{values.remainder.ToString()});
  return values.quotient;
}

namespace {
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
          test(L"DivisionZeroByZero", 0, 0, std::nullopt),
          test(L"NonPerfectDivision", 3, 2, std::nullopt),
          test(L"MediumDivision", 968, 11, L"88"),
          test(L"LargeNumbersDivision", 10000000, 100000, L"100"),
      });
    }());
}  // namespace

std::ostream& operator<<(std::ostream& os, const BigInt& p) {
  os << p.ToString();
  return os;
}

namespace {
const bool ostream_tests_registration =
    tests::Register(L"numbers::BigInt::OstreamOperator", [] {
      auto test = [](std::wstring name, BigInt input, std::string expectation) {
        return tests::Test{.name = name,
                           .callback = [input, expectation] mutable {
                             std::stringstream ss;
                             ss << input;
                             CHECK_EQ(ss.str(), expectation);
                           }};
      };

      return std::vector<tests::Test>(
          {test(L"Simple", BigInt::FromNumber(42), "42"),
           test(L"MultipleDigits", BigInt::FromNumber(1234), "1234"),
           test(L"Zero", BigInt(), "0")});
    }());
}  // namespace

language::ValueOrError<BigIntDivideOutput> Divide(BigInt numerator,
                                                  BigInt denominator) {
  DECLARE_OR_RETURN(NonZeroBigInt valid_demoninator,
                    NonZeroBigInt::New(std::move(denominator)));
  return Divide(std::move(numerator), std::move(valid_demoninator));
}

namespace {
const bool big_int_divide_tests_registration =
    tests::Register(L"numbers::BigInt::Divide", [] {
      auto test = [](std::wstring name, BigInt numerator, BigInt denominator,
                     BigInt expected_quotient, BigInt expected_remainder) {
        return tests::Test{.name = name,
                           .callback = [numerator, denominator,
                                        expected_quotient, expected_remainder] {
                             BigIntDivideOutput result = ValueOrDie(
                                 Divide(numerator, denominator), L"tests");
                             CHECK_EQ(result.quotient, expected_quotient);
                             CHECK_EQ(result.remainder, expected_remainder);
                           }};
      };

      return std::vector<tests::Test>{
          test(L"SimpleDivision", BigInt::FromNumber(10), BigInt::FromNumber(3),
               BigInt::FromNumber(3), BigInt::FromNumber(1)),
          test(L"DivisionByOne", BigInt::FromNumber(5), BigInt::FromNumber(1),
               BigInt::FromNumber(5), BigInt::FromNumber(0)),
          tests::Test{
              .name = L"ZeroDenominator",
              .callback =
                  [] {
                    CHECK(IsError(Divide(BigInt::FromNumber(5), BigInt())));
                  }},
      };
    }());
}  // namespace

BigIntDivideOutput Divide(BigInt numerator, NonZeroBigInt denominator) {
  BigInt quotient;
  BigInt current_dividend;

  for (size_t i = 0; i < numerator.digits.size(); ++i) {
    BigInt::Digit next = numerator.digits[numerator.digits.size() - 1 - i];
    CHECK_LE(next, 9ul);
    if (!current_dividend.digits.empty() || next != 0)
      current_dividend.digits.insert(current_dividend.digits.begin(), next);

    // Largest number such that denominator * x <= current_dividend:
    size_t x = 0;
    while (denominator.value() * BigInt::FromNumber(x + 1) <=
           current_dividend) {
      ++x;
      CHECK_LE(x, 10UL);
    }

    if (x > 0)
      current_dividend =
          ValueOrDie(std::move(current_dividend) -
                     denominator.value() * BigInt::FromNumber(x));
    CHECK_LE(x, 9ul);
    quotient.digits.insert(quotient.digits.begin(), x);
  }

  return BigIntDivideOutput{.quotient = BigInt(std::move(quotient.digits)),
                            .remainder = std::move(current_dividend)};
}

const bool big_int_divide_nonzero_tests_registration =
    tests::Register(L"numbers::NonZeroBigInt::Divide", [] {
      auto test = [](std::wstring name, BigInt numerator,
                     NonZeroBigInt denominator, BigInt expected_quotient,
                     BigInt expected_remainder) {
        return tests::Test{.name = name,
                           .callback = [numerator, denominator,
                                        expected_quotient, expected_remainder] {
                             BigIntDivideOutput result =
                                 Divide(numerator, denominator);
                             CHECK_EQ(result.quotient, expected_quotient);
                             CHECK_EQ(result.remainder, expected_remainder);
                           }};
      };

      return std::vector<tests::Test>{
          test(L"StandardDivision", BigInt::FromNumber(30),
               NonZeroBigInt::Constant<7>(), BigInt::FromNumber(4),
               BigInt::FromNumber(2)),
          test(L"EvenDivision", BigInt::FromNumber(24),
               NonZeroBigInt::Constant<6>(), BigInt::FromNumber(4),
               BigInt::FromNumber(0)),
          test(L"LargeNumerator", BigInt::FromNumber(10000),
               NonZeroBigInt::Constant<3>(), BigInt::FromNumber(3333),
               BigInt::FromNumber(1)),
          test(L"LargeDenominator", BigInt::FromNumber(5),
               NonZeroBigInt::Constant<10000>(), BigInt::FromNumber(0),
               BigInt::FromNumber(5)),
          test(L"BoundaryQuotientOfOne", BigInt::FromNumber(26),
               NonZeroBigInt::Constant<25>(), BigInt::FromNumber(1),
               BigInt::FromNumber(1)),
          test(L"DivisionByOne", BigInt::FromNumber(99),
               NonZeroBigInt::Constant<1>(), BigInt::FromNumber(99),
               BigInt::FromNumber(0)),
      };
    }());

language::ValueOrError<BigInt> operator%(BigInt numerator, BigInt denominator) {
  DECLARE_OR_RETURN(NonZeroBigInt non_zero_denominator,
                    NonZeroBigInt::New(std::move(denominator)));
  return numerator % non_zero_denominator;
}

namespace {
const bool big_int_modulo_tests_registration =
    tests::Register(L"numbers::BigInt::Modulo", [] {
      auto test = [](std::wstring name, BigInt numerator, BigInt denominator,
                     BigInt expected) {
        return tests::Test{
            .name = name, .callback = [numerator, denominator, expected] {
              CHECK_EQ(ValueOrDie(numerator % denominator), expected);
            }};
      };

      return std::vector<tests::Test>{
          tests::Test{.name = L"ZeroDenominator",
                      .callback =
                          [] {
                            CHECK(IsError(BigInt::FromNumber(10) %
                                          BigInt::FromNumber(0)));
                          }},
          test(L"StandardRemainder", BigInt::FromNumber(10),
               BigInt::FromNumber(3), BigInt::FromNumber(1)),
          test(L"NoRemainder", BigInt::FromNumber(12), BigInt::FromNumber(3),
               BigInt::FromNumber(0)),
          test(L"LargeNumeratorRemainder", BigInt::FromNumber(10000),
               BigInt::FromNumber(9999), BigInt::FromNumber(1))};
    }());

}  // namespace

BigInt BigInt::Pow(BigInt exponent) && {
  BigInt base = std::move(*this);
  BigInt output = BigInt::FromNumber(1);
  while (exponent > BigInt()) {
    BigIntDivideOutput divide_result =
        Divide(std::move(exponent), NonZeroBigInt::Constant<2>());
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
              BigInt output = std::move(base).Pow(std::move(exponent));
              CHECK(output == expectation);
            }};
      };

      return std::vector<tests::Test>({
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

ValueOrError<int32_t> BigInt::ToInt32() const { return ToNumber<int32_t>(); }

namespace {
const bool big_int_to_int32_tests_registration =
    tests::Register(L"numbers::BigInt::ToInt32", [] {
      auto test = [](std::wstring name, BigInt input,
                     std::optional<int32_t> expected) {
        return tests::Test{.name = name, .callback = [input, expected] {
                             ValueOrError<int32_t> result = input.ToInt32();
                             if (IsError(result))
                               CHECK(expected == std::nullopt);
                             else
                               CHECK_EQ(ValueOrDie(std::move(result), L"tests"),
                                        expected.value());
                           }};
      };

      return std::vector<tests::Test>{
          test(L"Zero", BigInt(), 0),
          test(L"Positive", BigInt::FromNumber(123), 123),
          test(L"Max", BigInt::FromNumber(std::numeric_limits<int32_t>::max()),
               std::numeric_limits<int32_t>::max()),
          test(L"Overflow",
               BigInt::FromNumber(std::numeric_limits<int32_t>::max()) +
                   BigInt::FromNumber(1),
               std::nullopt)};
    }());
}  // namespace

ValueOrError<int64_t> BigInt::ToInt64() const { return ToInt64(true); }

ValueOrError<int64_t> BigInt::ToInt64(bool positive) const {
  return ToNumber<int64_t>(positive);
}

namespace {
const bool big_int_to_int64_tests_registration =
    tests::Register(L"numbers::BigInt::ToInt64", [] {
      auto test = [](std::wstring name, BigInt input, bool positive,
                     std::optional<int64_t> expected) {
        return tests::Test{
            .name = name, .callback = [input, positive, expected] {
              ValueOrError<int64_t> result = input.ToInt64(positive);
              if (IsError(result))
                CHECK(expected == std::nullopt);
              else
                CHECK_EQ(ValueOrDie(std::move(result), L"tests"),
                         expected.value());
            }};
      };

      return std::vector<tests::Test>{
          test(L"Zero", BigInt(), true, 0),
          test(L"ZeroNegative", BigInt(), false, 0),
          test(L"Positive", BigInt::FromNumber(1234567890123), true,
               1234567890123),
          test(L"Negative", BigInt::FromNumber(1234567890123), false,
               -1234567890123),
          test(L"Max", BigInt::FromNumber(std::numeric_limits<int64_t>::max()),
               true, std::numeric_limits<int64_t>::max()),
          test(L"Overflow",
               BigInt::FromNumber(std::numeric_limits<int64_t>::max()) +
                   BigInt::FromNumber(1),
               true, std::nullopt)};
    }());
}  // namespace

language::ValueOrError<size_t> BigInt::ToSizeT() const {
  return ToNumber<size_t>();
}

namespace {
const bool big_int_to_size_t_tests_registration =
    tests::Register(L"numbers::BigInt::ToSizeT", [] {
      auto test = [](std::wstring name, BigInt input,
                     std::optional<size_t> expected) {
        return tests::Test{.name = name, .callback = [input, expected] {
                             ValueOrError<size_t> result = input.ToSizeT();
                             if (IsError(result))
                               CHECK(expected == std::nullopt);
                             else
                               CHECK_EQ(ValueOrDie(std::move(result), L"tests"),
                                        expected.value());
                           }};
      };

      return std::vector<tests::Test>{
          test(L"Zero", BigInt(), 0),
          test(L"Positive", BigInt::FromNumber(4294967295), 4294967295),
          test(L"Max", BigInt::FromNumber(std::numeric_limits<size_t>::max()),
               std::numeric_limits<size_t>::max()),
          test(L"Overflow",
               BigInt::FromNumber(std::numeric_limits<size_t>::max()) +
                   BigInt::FromNumber(1),
               std::nullopt)};
    }());
}  // namespace

language::ValueOrError<double> BigInt::ToDouble() const {
  double value = 0;
  for (double digit : digits | std::views::reverse) {
    value *= 10;
    value += digit;
  }
  return value;
}

namespace {
const bool big_int_to_double_tests_registration =
    tests::Register(L"numbers::BigInt::ToDouble", [] {
      auto test = [](std::wstring name, BigInt input,
                     std::optional<double> expected) {
        return tests::Test{.name = name, .callback = [input, expected] {
                             ValueOrError<double> result = input.ToDouble();
                             if (IsError(result))
                               CHECK(expected == std::nullopt);
                             else
                               CHECK_NEAR(
                                   ValueOrDie(std::move(result), L"tests"),
                                   expected.value(), 0.0001);
                           }};
      };

      return std::vector<tests::Test>{
          test(L"Zero", BigInt(), 0.0),
          test(L"Positive", BigInt::FromNumber(123456789012345),
               123456789012345.0),
          test(L"VeryLargeNumber",
               BigInt::FromNumber(10).Pow(BigInt::FromNumber(18)), 1e18)};
    }());
}  // namespace

/* static */ language::ValueOrError<NonZeroBigInt> NonZeroBigInt::New(
    BigInt value) {
  if (value.IsZero()) return NewError(LazyString{L"Expected non-zero value."});
  return NonZeroBigInt(std::move(value));
}

namespace {
const bool non_zero_big_int_factory_tests_registration = tests::Register(
    L"numbers::NonZeroBigInt::New",
    std::vector<tests::Test>{
        {.name = L"Zero",
         .callback = [] { CHECK(IsError(NonZeroBigInt::New(BigInt{}))); }},
        {.name = L"Positive", .callback = [] {
           CHECK_EQ(
               ValueOrDie(NonZeroBigInt::New(BigInt::FromNumber(1)), L"tests")
                   .value(),
               BigInt::FromNumber(1));
         }}});
}  // namespace

const BigInt& NonZeroBigInt::value() const { return value_; }

namespace {
const bool non_zero_big_int_value_tests_registration = tests::Register(
    L"numbers::NonZeroBigInt::Value",
    std::vector<tests::Test>{{.name = L"Constant", .callback = [] {
                                CHECK_EQ(
                                    NonZeroBigInt::Constant<7385>().value(),
                                    BigInt::FromNumber(7385));
                              }}});
}  // namespace

NonZeroBigInt::NonZeroBigInt(BigInt validated_value)
    : value_(std::move(validated_value)) {
  CHECK(!value_.IsZero());
}

NonZeroBigInt NonZeroBigInt::operator*(const NonZeroBigInt& b) const {
  return NonZeroBigInt(value_ * b.value_);
}

namespace {
const bool non_zero_big_int_multiplication_tests_registration = tests::Register(
    L"numbers::NonZeroBigInt::Multiplication",
    std::vector<tests::Test>{{.name = L"Identity",
                              .callback =
                                  [] {
                                    CHECK_EQ((NonZeroBigInt::Constant<7385>() *
                                              NonZeroBigInt::Constant<1>())
                                                 .value(),
                                             BigInt::FromNumber(7385));
                                  }},
                             {.name = L"Numbers", .callback = [] {
                                CHECK_EQ((NonZeroBigInt::Constant<73>() *
                                          NonZeroBigInt::Constant<29>()),
                                         NonZeroBigInt::Constant<2117>());
                              }}});
}  // namespace

// TODO(trivial): Add unit tests.
NonZeroBigInt NonZeroBigInt::Pow(BigInt exponent) && {
  // TODO(2024-04-09): Articulate better why the output is always positive.
  return NonZeroBigInt(std::move(value_).Pow(std::move(exponent)));
}

namespace {
const bool non_zero_big_int_pow_tests_registration = tests::Register(
    L"numbers::NonZeroBigInt::Pow",
    std::vector<tests::Test>{
        {.name = L"ZeroExponent",
         .callback =
             [] {
               CHECK_EQ(NonZeroBigInt::Constant<13>().Pow(BigInt{}),
                        NonZeroBigInt::Constant<1>());
             }},
        {.name = L"OneExponent",
         .callback =
             [] {
               CHECK_EQ(
                   NonZeroBigInt::Constant<13>().Pow(BigInt::FromNumber(1)),
                   NonZeroBigInt::Constant<13>());
             }},
        {.name = L"Numbers", .callback = [] {
           CHECK_EQ(NonZeroBigInt::Constant<13>().Pow(BigInt::FromNumber(5)),
                    NonZeroBigInt::Constant<371293>());
         }}});
}  // namespace

NonZeroBigInt NonZeroBigInt::GreatestCommonDivisor(
    const NonZeroBigInt& other) const {
  NonZeroBigInt a = *this;
  ValueOrError<NonZeroBigInt> b = other;

  while (!IsError(b)) {
    BigInt remainder = a.value() % std::get<NonZeroBigInt>(b);
    a = ValueOrDie(std::move(b));
    b = NonZeroBigInt::New(remainder);
  }
  return a;
}

namespace {
const bool gcd_tests_registration =
    tests::Register(L"numbers::BigInt::GreatestCommonDivisor", [] {
      auto test = [](std::wstring name, int input1, int input2,
                     int expectation) {
        return tests::Test{
            .name = name, .callback = [input1, input2, expectation] {
              NonZeroBigInt result =
                  ValueOrDie(NonZeroBigInt::New(BigInt::FromNumber(input1)))
                      .GreatestCommonDivisor(ValueOrDie(
                          NonZeroBigInt::New(BigInt::FromNumber(input2))));
              CHECK(result == ValueOrDie(NonZeroBigInt::New(
                                  BigInt::FromNumber(expectation))))
                  << "Unexpected GCD result for: " << input1 << " and "
                  << input2 << " yields " << result.value()
                  << ", expected: " << expectation;
            }};
      };

      return std::vector<tests::Test>({
          test(L"PositiveNumbers", 48, 18, 6),
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

// TODO(trivial): Add unit tests.
bool operator==(const NonZeroBigInt& a, const NonZeroBigInt& b) {
  return a.value() == b.value();
}

// TODO(trivial): Add unit tests.
BigInt operator%(BigInt numerator, NonZeroBigInt denominator) {
  return Divide(std::move(numerator), std::move(denominator)).remainder;
}

// TODO(trivial): Add unit tests.
std::ostream& operator<<(std::ostream& os, const NonZeroBigInt& p) {
  return os << p.value();
}
}  // namespace afc::math::numbers
