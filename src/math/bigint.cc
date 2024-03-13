#include "src/math/bigint.h"

#include <algorithm>  // For std::max
#include <cmath>      // For std::pow

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

using Digit = size_t;

class BigInt {
 public:
  bool positive = true;
  std::vector<Digit> digits;  // Element 0 is the list significant digit.

  BigInt() {}
  BigInt(bool positive_input, std::vector<Digit> digits_input)
      : positive(positive_input), digits(std::move(digits_input)) {
    RemoveLeadingZeros();
    for (Digit digit : digits) CHECK_LE(digit, 9UL);
  }

  static BigInt Positive(std::vector<Digit> digits) {
    return BigInt(true, std::move(digits));
  }

  static ValueOrError<BigInt> FromString(const std::wstring& input) {
    if (input.empty()) return NewError(LazyString{L"Input string is empty."});

    bool positive = input[0] != '-';
    size_t start = input[0] == L'-' || input[0] == L'+' ? 1 : 0;

    std::vector<Digit> digits;
    for (size_t i = start; i < input.size(); ++i) {
      if (input[i] < L'0' || input[i] > L'9') {
        return NewError(LazyString{L"Invalid character found: "} +
                        LazyString{ColumnNumberDelta{1}, input[i]});
      }
      digits.insert(digits.begin(), input[i] - L'0');
    }

    if (digits.empty())
      return NewError(LazyString{L"No digits found in input."});

    return BigInt(positive, std::move(digits));
  }

  std::wstring ToString() const {
    if (digits.empty()) return L"0";

    std::wstring result;
    if (!positive) result += L"-";

    for (auto it = digits.rbegin(); it != digits.rend(); ++it)
      result += static_cast<wchar_t>(L'0' + *it);
    return result;
  }

  template <typename NumberType>
  static BigInt FromNumber(NumberType value) {
    bool positive = value >= 0;
    if (!positive) value *= -1;
    std::vector<Digit> digits;
    do {
      digits.push_back(value % 10);
      value /= 10;
    } while (value != 0);
    return BigInt(positive, std::move(digits));
  }

 private:
  void RemoveLeadingZeros() {
    while (!digits.empty() && digits.back() == 0) digits.pop_back();
  }
};

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
          test(L"-1"),
          test(L"+1", {}, L"1"),
          test(L"123456789"),
          test(L"-123456789"),
          test(L"+123456789", {}, L"123456789"),
          test(L"0"),
          test(L"+0", {}, L"0"),
          test(L"-0", {}, L"0"),
          test(L"00001234", L"LeadingZeros", L"1234"),
          test(L"-00001234", L"NegativeLeadingZeros", L"-1234"),
          test(L"999999999999999999999999999999999999", L"Large",
               L"999999999999999999999999999999999999"),
          test(L"-999999999999999999999999999999999999", L"NegativeLarge",
               L"-999999999999999999999999999999999999"),
          test(std::wstring(100000, L'6'), L"VeryLarge"),
      });
    }());

BigInt Invert(BigInt a) {
  a.positive = !a.positive;
  return a;
}

bool operator==(const BigInt& a, const BigInt& b) {
  return a.positive == b.positive && a.digits == b.digits;
}

bool operator>(const BigInt& a, const BigInt& b) {
  if (a.positive && !b.positive) return true;
  if (b.positive && !a.positive) return false;
  if (!a.positive && !b.positive) return Invert(b) > Invert(a);
  if (a.digits.size() != b.digits.size())
    return a.digits.size() > b.digits.size();
  for (auto it_a = a.digits.rbegin(), it_b = b.digits.rbegin();
       it_a != a.digits.rend(); ++it_a, ++it_b)
    if (*it_a != *it_b) return *it_a > *it_b;
  return false;
}

bool operator<(const BigInt& a, const BigInt& b) { return b > a; }
bool operator>=(const BigInt& a, const BigInt& b) { return a > b || a == b; }
bool operator<=(const BigInt& a, const BigInt& b) { return b >= a; }

BigInt operator+(BigInt a, BigInt b) {
  if (!a.positive && !b.positive)
    return Invert(Invert(std::move(a)) + Invert(std::move(b)));
  if (!b.positive) return std::move(a) - Invert(std::move(b));
  if (!a.positive) return std::move(b) - Invert(std::move(a));

  std::vector<Digit> result;

  const size_t max_size = std::max(a.digits.size(), b.digits.size());
  result.resize(max_size, 0);

  size_t carry = 0;
  for (size_t i = 0; i < max_size; ++i) {
    Digit a_digit = i < a.digits.size() ? a.digits[i] : 0;
    Digit b_digit = i < b.digits.size() ? b.digits[i] : 0;
    CHECK_LE(a_digit, 9UL);
    CHECK_LE(b_digit, 9UL);
    Digit sum = a_digit + b_digit + carry;
    carry = sum / 10;
    CHECK_LE(carry, 1ul);
    result[i] = sum % 10;
  }

  if (carry > 0) result.push_back(carry);
  return BigInt::Positive(std::move(result));
}

BigInt operator-(BigInt a, BigInt b) {
  if (!a.positive && !b.positive)
    return Invert(Invert(std::move(a)) - Invert(std::move(b)));
  if (!b.positive) return std::move(a) + Invert(std::move(b));
  if (!a.positive) return Invert(Invert(std::move(a)) + std::move(b));
  if (a < b) return Invert(std::move(b) - std::move(a));

  std::vector<Digit> digits;
  int borrow = 0;
  for (size_t i = 0; i < a.digits.size(); ++i) {
    Digit a_digit = i < a.digits.size() ? a.digits[i] : 0;
    Digit b_digit = i < b.digits.size() ? b.digits[i] : 0;
    int diff = a_digit - b_digit - borrow;
    if (diff < 0) {
      diff += 10;
      CHECK_GE(diff, 0);
      borrow = 1;
    } else {
      borrow = 0;
    }
    digits.push_back(diff);
  }

  return BigInt::Positive(std::move(digits));
}

BigInt operator*(BigInt a, BigInt b) {
  std::vector<Digit> digits;
  // Largest possible result:
  digits.resize(a.digits.size() + b.digits.size(), 0);

  // Perform multiplication digit by digit:
  for (size_t i = 0; i < a.digits.size(); ++i) {
    for (size_t j = 0; j < b.digits.size(); ++j) {
      int product = a.digits[i] * b.digits[j];
      digits[i + j] += product;

      for (size_t k = i + j; digits[k] >= 10; ++k) {  // Handle carry.
        digits[k + 1] += digits[k] / 10;
        digits[k] %= 10;
      }
    }
  }
  return BigInt(a.positive == b.positive, std::move(digits));
}

language::ValueOrError<BigInt> operator/(BigInt numerator, BigInt denominator) {
  if (denominator.digits.empty())
    return NewError(LazyString{L"Division by zero."});

  bool output_positive = numerator.positive == denominator.positive;
  if (!numerator.positive) numerator = Invert(std::move(numerator));
  if (!denominator.positive) denominator = Invert(std::move(denominator));

  BigInt quotient;
  BigInt current_dividend;

  for (size_t i = 0; i < numerator.digits.size(); ++i) {
    Digit next = numerator.digits[numerator.digits.size() - 1 - i];
    CHECK_LE(next, 9ul);
    if (!current_dividend.digits.empty() || next != 0)
      current_dividend.digits.insert(current_dividend.digits.begin(), next);

    // Largest number such that denominator * x <= current_dividend:
    size_t x = 0;
    while (denominator * BigInt(true, {static_cast<Digit>(x + 1)}) <=
           current_dividend)
      ++x;

    if (x > 0)
      current_dividend = current_dividend -
                         denominator * BigInt(true, {static_cast<Digit>(x)});
    CHECK_LE(x, 9ul);
    quotient.digits.insert(quotient.digits.begin(), x);
  }

  if (!current_dividend.digits.empty())
    return NewError(LazyString{L"Non-empty reminder: "} +
                    LazyString{current_dividend.ToString()});
  // In case we inserted leading zeros.
  return BigInt(output_positive, std::move(quotient.digits));
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
          test(L"NegativeNumbers", -45, -123, true),
          test(L"PositiveGreaterThanZero", 123, 0, true),
          test(L"ZeroNotGreaterThanPositive", 0, 123, false),
          test(L"ZeroGreaterThanNegative", 0, -123, true),
          test(L"EqualNumbers", 100, 100, false),
          test(L"LargeNumbers", 1000000001, 1000000000, true),
          test(L"DifferentLengthsPositive", 12345, 123, true),
          test(L"DifferentLengthsNegative", -123, -12345, true),
          test(L"CloseValues", 1001, 1000, true),
      });
    }());

const bool addition_tests_registration =
    tests::Register(L"numbers::BigInt::Addition", [] {
      auto test = [](std::wstring name, BigInt input1, BigInt input2,
                     std::wstring expectation) {
        return tests::Test{.name = name,
                           .callback = [input1, input2, expectation] {
                             std::wstring output = (input1 + input2).ToString();
                             CHECK(output == expectation);
                           }};
      };

      return std::vector<tests::Test>({
          test(L"Positive", BigInt::FromNumber(123), BigInt::FromNumber(456),
               L"579"),
          test(L"NegativeFirst", BigInt::FromNumber(-123),
               BigInt::FromNumber(456), L"333"),
          test(L"NegativeSecond", BigInt::FromNumber(123),
               BigInt::FromNumber(-456), L"-333"),
          test(L"BothNegative", BigInt::FromNumber(-123),
               BigInt::FromNumber(-456), L"-579"),
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
          test(L"NegativeResult", BigInt::FromNumber(-123),
               BigInt::FromNumber(23), L"-100"),
          test(L"SameMagnitudePositiveNegative", BigInt::FromNumber(123),
               BigInt::FromNumber(-123), L"0"),
          test(L"EdgeCaseLargeSum",
               ValueOrDie(BigInt::FromString(L"18446744073709551615")),
               BigInt::FromNumber(1), L"18446744073709551616"),
      });
    }());

const bool subtraction_tests_registration =
    tests::Register(L"numbers::BigInt::Subtraction", [] {
      auto test = [](std::wstring name, BigInt input1, BigInt input2,
                     std::wstring expectation) {
        return tests::Test{.name = name,
                           .callback = [input1, input2, expectation] {
                             std::wstring output = (input1 - input2).ToString();
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
          test(L"ZeroMinusNumber", BigInt::FromNumber(0),
               BigInt::FromNumber(123), L"-123"),
          test(L"NegativeResult", BigInt::FromNumber(100),
               BigInt::FromNumber(204), L"-104"),
          test(L"NegativeMinuend", BigInt::FromNumber(-123),
               BigInt::FromNumber(100), L"-223"),
          test(L"NegativeSubtrahend", BigInt::FromNumber(123),
               BigInt::FromNumber(-100), L"223"),
          test(L"BothNegativeGreaterMinuend", BigInt::FromNumber(-100),
               BigInt::FromNumber(-200), L"100"),
          test(L"BothNegativeLesserMinuend", BigInt::FromNumber(-200),
               BigInt::FromNumber(-100), L"-100"),
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
          test(L"MultiplicationWithOne", BigInt::FromNumber(-12345),
               BigInt::FromNumber(1), L"-12345"),
          test(L"SingleDigitMultiplicationRequiringCarry",
               BigInt::FromNumber(9), BigInt::FromNumber(9), L"81"),
          test(L"MultipleDigitsWithCarry", BigInt::FromNumber(15),
               BigInt::FromNumber(27), L"405"),
          test(L"NegativeMultiplication", BigInt::FromNumber(-12),
               BigInt::FromNumber(34), L"-408"),
          test(L"TwoNegativeNumbers", BigInt::FromNumber(-12),
               BigInt::FromNumber(-34), L"408"),
          test(L"ZeroMultiplicationLargeNumber", BigInt::FromNumber(0),
               ValueOrDie(BigInt::FromString(std::wstring(1000, L'9'))), L"0"),
          test(L"LargeNumberMultiplication",
               ValueOrDie(BigInt::FromString(std::wstring(50, L'9'))),
               ValueOrDie(BigInt::FromString(L"1" + std::wstring(100, L'0'))),
               std::wstring(50, L'9') + std::wstring(100, L'0')),
          test(L"MultiplicationByNegativeOne", BigInt::FromNumber(-1),
               BigInt::FromNumber(12345), L"-12345"),
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
          test(L"NegativeDivision", -4, 2, L"-2"),
          test(L"DivisionOfNegatives", -4, -2, L"2"),
          test(L"LargeNumbersDivision", 100000, 1000, L"100"),
      });
    }());

}  // namespace
}  // namespace afc::math::numbers
