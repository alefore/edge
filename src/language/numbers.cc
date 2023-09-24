#include "src/language/numbers.h"

#include <glog/logging.h>

#include <ranges>

#include "src/language/error/value_or_error.h"
#include "src/language/overload.h"
#include "src/tests/tests.h"

namespace afc::language::numbers {
namespace {

// Least significative digit first. Zero should always be represented as the
// empty vector (never as {0}).
GHOST_TYPE_CONTAINER(Digits, std::vector<size_t>);

struct Decimal {
  bool positive = true;
  bool exact = true;
  Digits digits;
};

std::wstring ToString(const Decimal& decimal, size_t decimal_digits) {
  std::wstring output;
  if (!decimal.positive) output.push_back(L'-');
  bool has_dot = false;
  if (decimal_digits >= decimal.digits.size()) {
    output.push_back(L'0');
    if (decimal_digits > decimal.digits.size()) {
      output.push_back(L'.');
      has_dot = true;
      for (size_t i = 0; i < decimal_digits - decimal.digits.size(); ++i)
        output.push_back(L'0');
    }
  }
  for (size_t i = 0; i < decimal.digits.size(); i++) {
    if (i == decimal.digits.size() - decimal_digits) {
      has_dot = true;
      output.push_back(L'.');
    }
    output.push_back(L'0' + decimal.digits[decimal.digits.size() - 1 - i]);
  }
  if (decimal.exact && has_dot) {
    while (!output.empty() && output.back() == L'0') output.pop_back();
    if (!output.empty() && output.back() == L'.') output.pop_back();
  }
  return output;
}

Digits RemoveSignificantZeros(Digits value) {
  while (!value.empty() && value.back() == 0) value.pop_back();
  return value;
}

Decimal ToDecimalBase(int value, size_t decimal_digits) {
  LOG(INFO) << "Representing int: " << value;
  Decimal output{.positive = value >= 0,
                 .digits = {Digits(std::vector<size_t>(decimal_digits, 0))}};
  if (value < 0) value = -value;
  while (value != 0) {
    output.digits.push_back(value % 10);
    value /= 10;
  }
  output.digits = RemoveSignificantZeros(output.digits);
  return output;
}

Digits RemoveDecimals(Digits value, size_t digits_to_remove) {
  if (digits_to_remove == 0) return value;
  if (digits_to_remove > value.size()) return Digits();
  int carry = value[digits_to_remove - 1] >= 5 ? 1 : 0;
  value.erase(value.begin(), value.begin() + digits_to_remove);
  for (size_t i = 0; i < value.size() && carry > 0; ++i) {
    value[i] += carry;
    carry = value[i] / 10;
    value[i] = value[i] % 10;
  }
  if (carry) value.push_back(carry);
  return value;
}

const bool remove_decimals_tests__registration =
    tests::Register(L"numbers::RemoveDecimals", [] {
      auto test = [](std::wstring input, size_t digits,
                     std::wstring expectation) {
        return tests::Test(
            {.name = input, .callback = [=] {
               Digits input_digits;
               for (wchar_t c : input | std::views::reverse)
                 input_digits.push_back(c - L'0');
               std::wstring str = ToString(
                   {.digits = RemoveDecimals(input_digits, digits)}, 0);
               LOG(INFO) << "From [" << ToString({.digits = input_digits}, 0)
                         << "] → [" << str << "]";
               CHECK(str == expectation);
             }});
      };
      return std::vector({
          test(L"45", 2, L"0"),
          test(L"12", 0, L"12"),
          test(L"12345", 3, L"12"),
          test(L"198", 1, L"20"),
          test(L"19951", 2, L"200"),
          test(L"9951", 2, L"100"),
          test(L"16", 1, L"2"),
          test(L"6", 1, L"1"),
      });
    }());

bool operator>(const Digits& a, const Digits& b) {
  if (a.size() != b.size()) return a.size() > b.size();
  for (auto it_a = a.rbegin(), it_b = b.rbegin(); it_a != a.rend();
       ++it_a, ++it_b)
    if (*it_a != *it_b) return *it_a > *it_b;
  return false;
}

bool operator<(const Digits& a, const Digits& b) { return b > a; }
bool operator>=(const Digits& a, const Digits& b) { return a > b || a == b; }
bool operator<=(const Digits& a, const Digits& b) { return b >= a; }

Digits operator+(const Digits& a, const Digits& b) {
  int carry = 0;
  Digits output;
  for (size_t digit = 0; a.size() > digit || b.size() > digit || carry > 0;
       digit++) {
    if (a.size() > digit) carry += a[digit];
    if (b.size() > digit) carry += b[digit];
    output.push_back(carry % 10);
    carry /= 10;
  }
  return output;
}

Digits operator-(const Digits& a, const Digits& b) {
  CHECK(a >= b);
  int borrow = 0;
  Digits output;
  for (size_t digit = 0; a.size() > digit || b.size() > digit; digit++) {
    int output_digit = ((a.size() > digit) ? a[digit] : 0) -
                       ((b.size() > digit) ? b[digit] : 0) - borrow;
    if (output_digit < 0) {
      output_digit += 10;
      borrow = 1;
    } else {
      borrow = 0;
    }
    output.push_back(output_digit);
  }
  return RemoveSignificantZeros(std::move(output));
}

Digits operator*(const Digits& a, const Digits& b) {
  Digits result(std::vector<size_t>(a.size() + b.size(), 0));
  for (size_t i = 0; i < a.size(); ++i) {
    for (size_t j = 0; j < b.size(); ++j) {
      int product = a[i] * b[j];
      result[i + j] += product;
      for (size_t k = i + j; result[k] >= 10; ++k) {  // Handle any carry.
        result[k + 1] += result[k] / 10;
        result[k] %= 10;
      }
    }
  }
  return RemoveSignificantZeros(std::move(result));
}

bool operator==(const Decimal& a, const Decimal& b) {
  return a.positive == b.positive && a.digits == b.digits;
}

bool operator<(const Decimal& a, const Decimal& b) {
  if (a.positive && !b.positive) return false;
  if (!a.positive && b.positive) return true;
  return (a.positive && b.positive) ? a.digits < b.digits : b.digits < a.digits;
}

ValueOrError<Digits> DivideDigits(const Digits& dividend, const Digits& divisor,
                                  size_t extra_precision) {
  LOG(INFO) << "Dividing: " << divisor.size();
  if (divisor.empty()) return Error(L"Division by zero.");
  Digits quotient;
  Digits current_dividend;
  for (size_t i = 0; i < dividend.size() + extra_precision; ++i) {
    current_dividend.insert(
        current_dividend.begin(),
        i < dividend.size() ? dividend[dividend.size() - 1 - i] : 0);
    size_t x = 0;  // Largest number such that divisor * x <= current_dividend.
    while (divisor * Digits({x + 1}) <= current_dividend) ++x;
    CHECK_LE(x, 9ul);
    if (x > 0) current_dividend = current_dividend - divisor * Digits({x});
    quotient.insert(quotient.begin(), std::move(x));
  }
  return RemoveSignificantZeros(quotient);
}

ValueOrError<Decimal> ToDecimal(const Number& number, size_t decimal_digits);

ValueOrError<Decimal> ToDecimalBase(Addition value, size_t decimal_digits) {
  ASSIGN_OR_RETURN(Decimal a, ToDecimal(value.a.value(), decimal_digits + 1));
  ASSIGN_OR_RETURN(Decimal b, ToDecimal(value.b.value(), decimal_digits + 1));
  if (a.positive == b.positive)
    return Decimal{.positive = a.positive,
                   .exact = a.exact && b.exact,
                   .digits = RemoveDecimals(a.digits + b.digits, 1)};
  else if (a.digits > b.digits)
    return Decimal{.positive = a.positive,
                   .exact = a.exact && b.exact,
                   .digits = RemoveDecimals(a.digits - b.digits, 1)};
  else
    return Decimal{.positive = b.positive,
                   .exact = a.exact && b.exact,
                   .digits = RemoveDecimals(b.digits - a.digits, 1)};
}

ValueOrError<Decimal> ToDecimalBase(Negation value, size_t decimal_digits) {
  ASSIGN_OR_RETURN(Decimal output, ToDecimal(value.a.value(), decimal_digits));
  output.positive = !output.positive;
  return output;
}

ValueOrError<Decimal> ToDecimalBase(Multiplication value,
                                    size_t decimal_digits) {
  // TODO(2023-09-21): This can be optimized to compute fewer decimal digits
  // in the recursions.
  ASSIGN_OR_RETURN(Decimal a, ToDecimal(value.a.value(), decimal_digits));
  ASSIGN_OR_RETURN(Decimal b, ToDecimal(value.b.value(), decimal_digits));
  return Decimal{.positive = a.positive == b.positive,
                 .exact = a.exact && b.exact,
                 .digits = RemoveDecimals(a.digits * b.digits, decimal_digits)};
}

ValueOrError<Decimal> ToDecimalBase(Division value, size_t decimal_digits) {
  ASSIGN_OR_RETURN(Decimal a, ToDecimal(value.a.value(), decimal_digits));
  ASSIGN_OR_RETURN(Decimal b, ToDecimal(value.b.value(), decimal_digits));
  ASSIGN_OR_RETURN(Digits output,
                   DivideDigits(a.digits, b.digits, decimal_digits));
  // TODO(2023-09-23, numbers): Compute `exact`? If both are exact, check that
  // one is an exact multiple of the other?
  return Decimal{.positive = a.positive == b.positive,
                 .exact = false,
                 .digits = std::move(output)};
}

ValueOrError<Decimal> ToDecimal(const Number& number, size_t decimal_digits) {
  return std::visit(
      [decimal_digits](const auto& value) -> ValueOrError<Decimal> {
        return ToDecimalBase(value, decimal_digits);
      },
      number);
}

const bool as_decimal_tests_registration =
    tests::Register(L"numbers::ToDecimal", [] {
      auto test = [](Number number, std::wstring expectation) {
        return tests::Test(
            {.name = expectation, .callback = [=] {
               std::wstring str = std::visit(
                   overload{[](Error error) { return error.read(); },
                            [](Decimal d) { return ToString(d, 2); }},
                   ToDecimal(number, 2));
               LOG(INFO) << "Representation: " << str;
               CHECK(str == expectation);
             }});
      };
      return std::vector(
          {test(45, L"45"),
           test(0, L"0"),
           test(-328, L"-328"),
           test(Addition{MakeNonNullShared<Number>(1),
                         MakeNonNullShared<Number>(0)},
                L"1"),
           test(Addition{MakeNonNullShared<Number>(Number(7)),
                         MakeNonNullShared<Number>(5)},
                L"12"),
           test(Addition{MakeNonNullShared<Number>(Number(7)),
                         MakeNonNullShared<Number>(-5)},
                L"2"),
           test(Addition{MakeNonNullShared<Number>(Number(7)),
                         MakeNonNullShared<Number>(-30)},
                L"-23"),
           test(Addition{MakeNonNullShared<Number>(Number(-7)),
                         MakeNonNullShared<Number>(-30)},
                L"-37"),
           test(Addition{MakeNonNullShared<Number>(Number(-100)),
                         MakeNonNullShared<Number>(30)},
                L"-70"),
           test(Addition{MakeNonNullShared<Number>(2147483647),
                         MakeNonNullShared<Number>(2147483647)},
                L"4294967294"),
           test(Multiplication{MakeNonNullShared<Number>(1),
                               MakeNonNullShared<Number>(10)},
                L"10"),
           test(Multiplication{MakeNonNullShared<Number>(-2),
                               MakeNonNullShared<Number>(25)},
                L"-50"),
           test(Multiplication{MakeNonNullShared<Number>(-1),
                               MakeNonNullShared<Number>(-35)},
                L"35"),
           test(Multiplication{MakeNonNullShared<Number>(11),
                               MakeNonNullShared<Number>(12)},
                L"132"),
           test(Multiplication{MakeNonNullShared<Number>(-1),
                               MakeNonNullShared<Number>(
                                   Addition{MakeNonNullShared<Number>(2),
                                            MakeNonNullShared<Number>(3)})},
                L"-5"),
           test(Multiplication{MakeNonNullShared<Number>(2147483647),
                               MakeNonNullShared<Number>(2147483647)},
                L"4611686014132420609"),
           test(Addition{MakeNonNullShared<Number>(Multiplication{
                             MakeNonNullShared<Number>(2147483647),
                             MakeNonNullShared<Number>(2147483647)}),
                         MakeNonNullShared<Number>(
                             Division{MakeNonNullShared<Number>(3),
                                      MakeNonNullShared<Number>(100)})},
                L"4611686014132420609.03"),
           test(Division{MakeNonNullShared<Number>(3),
                         MakeNonNullShared<Number>(10)},
                L"0.30"),
           test(Addition{MakeNonNullShared<Number>(
                             Multiplication{MakeNonNullShared<Number>(20),
                                            MakeNonNullShared<Number>(20)}),
                         MakeNonNullShared<Number>(
                             Division{MakeNonNullShared<Number>(3),
                                      MakeNonNullShared<Number>(100)})},
                L"400.03"),
           test(Division{MakeNonNullShared<Number>(1),
                         MakeNonNullShared<Number>(3)},
                L"0.33"),
           test(Addition{MakeNonNullShared<Number>(
                             Division{MakeNonNullShared<Number>(1),
                                      MakeNonNullShared<Number>(300)}),
                         MakeNonNullShared<Number>(
                             Division{MakeNonNullShared<Number>(1),
                                      MakeNonNullShared<Number>(300)})},
                L"0.01"),
           test(Division{MakeNonNullShared<Number>(10),
                         MakeNonNullShared<Number>(0)},
                L"Division by zero.")});
    }());
}  // namespace

ValueOrError<std::wstring> ToString(const Number& number,
                                    size_t decimal_digits) {
  ASSIGN_OR_RETURN(Decimal decimal, ToDecimal(number, decimal_digits));
  return ToString(decimal, decimal_digits);
}

ValueOrError<int> ToInt(const Number& number) {
  ASSIGN_OR_RETURN(Decimal decimal, ToDecimal(number, 0));
  if (!decimal.exact)
    return Error(L"Inexact numbers can't be represented as integer.");
  // TODO(P1, 2023-09-23): Check bounds and fail if the number is too large or
  // small.
  int value = 0;
  for (int digit : decimal.digits | std::views::reverse)
    value = value * 10 + (decimal.positive ? 1 : -1) * digit;
  return value;
}

ValueOrError<double> ToDouble(const Number& number) {
  static constexpr size_t kDefaultPrecision = 6;
  static constexpr double kFinalDivision = 10e5;

  ASSIGN_OR_RETURN(Decimal decimal, ToDecimal(number, kDefaultPrecision));
  double value = 0;
  for (int digit : decimal.digits | std::views::reverse)
    value = value * 10 + (decimal.positive ? 1 : -1) * digit;
  return value / kFinalDivision;
}

Number FromDouble(double value) {
  static constexpr int kFinalDivision = 10e5;
  return Division{
      MakeNonNullShared<Number>(static_cast<int>(value * kFinalDivision)),
      MakeNonNullShared<Number>(kFinalDivision)};
}

namespace {
const bool double_tests_registration = tests::Register(
    L"numbers::Double",
    {{.name = L"FromDouble",
      .callback =
          [] { CHECK(ValueOrDie(ToString(FromDouble(5.0), 2)) == L"5.00"); }},
     {.name = L"ToDouble",
      .callback =
          [] { CHECK_NEAR(ValueOrDie(ToDouble(Number(5))), 5.0, 0.00001); }},
     {.name = L"5", .callback = [] {
        CHECK_NEAR(ValueOrDie(ToDouble(FromDouble(5))), 5.0, 0.00001);
      }}});
}

Number FromSizeT(size_t value) {
  // TODO(P1, 2023-09-23): Detect and handle overflows.
  return Number{static_cast<int>(value)};
}

ValueOrError<size_t> ToSizeT(const Number& number) {
  // TODO(P1, 2023-09-23): Handle overflows.
  ASSIGN_OR_RETURN(int value_int, ToInt(number));
  if (value_int < 0)
    return Error(L"Negative numbers can't be represented as size_t.");
  return static_cast<size_t>(value_int);
}

ValueOrError<bool> IsEqual(const Number& a, const Number& b, size_t precision) {
  ASSIGN_OR_RETURN(Decimal a_decimal, ToDecimal(a, precision));
  ASSIGN_OR_RETURN(Decimal b_decimal, ToDecimal(b, precision));
  return a_decimal == b_decimal;
}

ValueOrError<bool> IsLessThan(const Number& a, const Number& b,
                              size_t precision) {
  ASSIGN_OR_RETURN(Decimal a_decimal, ToDecimal(a, precision));
  ASSIGN_OR_RETURN(Decimal b_decimal, ToDecimal(b, precision));
  return a_decimal < b_decimal;
}

ValueOrError<bool> IsLessThanOrEqual(const Number& a, const Number& b,
                                     size_t precision) {
  ASSIGN_OR_RETURN(Decimal a_decimal, ToDecimal(a, precision));
  ASSIGN_OR_RETURN(Decimal b_decimal, ToDecimal(b, precision));
  return a_decimal < b_decimal || a_decimal == b_decimal;
}

};  // namespace afc::language::numbers
