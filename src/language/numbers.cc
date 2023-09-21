#include "src/language/numbers.h"

#include <glog/logging.h>

#include <ranges>

#include "src/language/error/value_or_error.h"
#include "src/language/overload.h"
#include "src/tests/tests.h"

namespace afc::language::numbers {

Decimal AsDecimalBase(int value, size_t decimal_digits) {
  LOG(INFO) << "Representing int: " << value;
  Decimal output{.positive = value >= 0,
                 .digits = {std::vector<size_t>(decimal_digits, 0)}};
  if (value < 0) value = -value;
  while (value != 0) {
    output.digits.value.push_back(value % 10);
    value /= 10;
  }
  return output;
}

Digits RemoveSignificantZeros(Digits value) {
  while (value.value.size() > 1 && value.value.back() == 0)
    value.value.pop_back();
  return value;
}

Digits RemoveDecimals(Digits value, size_t digits_to_remove) {
  if (digits_to_remove == 0) return value;
  if (digits_to_remove > value.value.size()) return Digits();
  int carry = value.value[digits_to_remove - 1] >= 5 ? 1 : 0;
  std::vector<size_t> output(
      std::next(value.value.begin() + digits_to_remove - 1), value.value.end());
  for (size_t i = 0; i < output.size() && carry > 0; ++i) {
    output[i] += carry;
    carry = output[i] / 10;
    output[i] = output[i] % 10;
  }
  if (carry) output.push_back(carry);
  return Digits{.value = output};
}

namespace {
const bool remove_decimals_tests__registration =
    tests::Register(L"numbers::RemoveDecimals", [] {
      auto test = [](std::wstring input, size_t digits,
                     std::wstring expectation) {
        return tests::Test(
            {.name = input, .callback = [=] {
               Digits input_digits;
               for (wchar_t c : input | std::views::reverse)
                 input_digits.value.push_back(c - L'0');
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
}

bool operator>(const Digits& a, const Digits& b) {
  if (a.value.size() != b.value.size()) return a.value.size() > b.value.size();
  for (auto it_a = a.value.rbegin(), it_b = b.value.rbegin();
       it_a != a.value.rend(); ++it_a, ++it_b) {
    if (*it_a > *it_b) return true;
    if (*it_a < *it_b) return false;
  }
  return false;
}

bool operator==(const Digits& a, const Digits& b) {
  if (a.value.size() != b.value.size()) return false;
  for (auto it_a = a.value.rbegin(), it_b = b.value.rbegin();
       it_a != a.value.rend(); ++it_a, ++it_b)
    if (*it_a != *it_b) return false;
  return true;
}

bool operator>=(const Digits& a, const Digits& b) { return a > b || a == b; }
bool operator<=(const Digits& a, const Digits& b) { return b >= a; }
bool operator<(const Digits& a, const Digits& b) { return !(a >= b); }

Digits operator+(const Digits& a, const Digits& b) {
  int carry = 0;
  Digits output;
  for (size_t digit = 0;
       a.value.size() > digit || b.value.size() > digit || carry > 0; digit++) {
    if (a.value.size() > digit) carry += a.value[digit];
    if (b.value.size() > digit) carry += b.value[digit];
    output.value.push_back(carry % 10);
    carry /= 10;
  }
  return output;
}

Digits operator-(const Digits& a, const Digits& b) {
  CHECK(a >= b);
  int borrow = 0;
  Digits output;
  for (size_t digit = 0; a.value.size() > digit || b.value.size() > digit;
       digit++) {
    int output_digit = ((a.value.size() > digit) ? a.value[digit] : 0) -
                       ((b.value.size() > digit) ? b.value[digit] : 0) - borrow;

    if (output_digit < 0) {
      output_digit += 10;
      borrow = 1;
    } else {
      borrow = 0;
    }

    output.value.push_back(output_digit);
  }

  // Remove leading zeros from the result.
  while (output.value.size() > 1 && output.value.back() == 0) {
    output.value.pop_back();
  }

  return RemoveSignificantZeros(std::move(output));
}

Digits operator*(const Digits& a, const Digits& b) {
  Digits result{.value =
                    std::vector<size_t>(a.value.size() + b.value.size(), 0)};
  for (size_t i = 0; i < a.value.size(); ++i) {
    for (size_t j = 0; j < b.value.size(); ++j) {
      int product = a.value[i] * b.value[j];
      result.value[i + j] += product;
      // Handle any carry.
      for (size_t k = i + j; result.value[k] >= 10; ++k) {
        result.value[k + 1] += result.value[k] / 10;
        result.value[k] %= 10;
      }
    }
  }

  return RemoveSignificantZeros(std::move(result));
}

ValueOrError<Digits> DivideDigits(const Digits& dividend, const Digits& divisor,
                                  size_t extra_precision) {
  if (divisor.value.empty()) return Error(L"Division by zero!");

  Digits quotient;
  Digits current_dividend;

  for (size_t i = 0; i < dividend.value.size() + extra_precision; ++i) {
    LOG(INFO) << "Iteration: " << i;
    current_dividend.value.insert(
        current_dividend.value.begin(),
        i < dividend.value.size()
            ? dividend.value[dividend.value.size() - 1 - i]
            : 0);
    LOG(INFO) << "Inserted, current: "
              << ToString({.digits = current_dividend}, 0) << ", divisor "
              << ToString({.digits = divisor}, 0) << " first: "
              << ToString({.digits = (divisor * Digits{.value = {1}})}, 0)
              << ", enter: "
              << (divisor * Digits{.value = {1}} < current_dividend);
    // Largest number x such that divisor * x <= current_dividend
    size_t x = 0;
    while (divisor * Digits{.value = {x + 1}} <= current_dividend) ++x;
    LOG(INFO) << "X is: " << x;
    CHECK_LE(x, 9ul);
    if (x > 0)
      current_dividend = current_dividend - divisor * Digits{.value = {x}};
    if (x != 0 || !quotient.value.empty())
      quotient.value.insert(quotient.value.begin(), x);
  }
  LOG(INFO) << "Returning: " << ToString({.digits = quotient}, 0);
  return quotient;
}

ValueOrError<Decimal> AsDecimalBase(Addition value, size_t decimal_digits) {
  LOG(INFO) << "Sum decimal_digits: " << decimal_digits;
  ASSIGN_OR_RETURN(Decimal a, AsDecimal(value.a.value(), decimal_digits + 1));
  ASSIGN_OR_RETURN(Decimal b, AsDecimal(value.b.value(), decimal_digits + 1));
  LOG(INFO) << "Addition: " << ToString(a, 0) << ", " << ToString(b, 0) << " = "
            << ToString({.digits = a.digits + b.digits}, 0) << " "
            << ToString({.digits = RemoveDecimals(a.digits + b.digits, 1)}, 0);
  if (a.positive == b.positive) {
    return Decimal{.positive = a.positive,
                   .digits = RemoveDecimals(a.digits + b.digits, 1)};
  } else if (a.digits > b.digits) {
    return Decimal{.positive = a.positive,
                   .digits = RemoveDecimals(a.digits - b.digits, 1)};
  } else {
    return Decimal{.positive = b.positive,
                   .digits = RemoveDecimals(b.digits - a.digits, 1)};
  }
}

ValueOrError<Decimal> AsDecimalBase(Multiplication value,
                                    size_t decimal_digits) {
  // TODO(2023-09-21): This can be optimized to compute fewer decimal digits
  // in the recursions.
  ASSIGN_OR_RETURN(Decimal a, AsDecimal(value.a.value(), decimal_digits));
  ASSIGN_OR_RETURN(Decimal b, AsDecimal(value.b.value(), decimal_digits));
  return Decimal{.positive = a.positive == b.positive,
                 .digits = RemoveDecimals(a.digits * b.digits, decimal_digits)};
}

ValueOrError<Decimal> AsDecimalBase(Division value, size_t decimal_digits) {
  ASSIGN_OR_RETURN(Decimal a, AsDecimal(value.a.value(), decimal_digits));
  ASSIGN_OR_RETURN(Decimal b, AsDecimal(value.b.value(), decimal_digits));
  ASSIGN_OR_RETURN(Digits output,
                   DivideDigits(a.digits, b.digits, decimal_digits));
  return Decimal{.positive = a.positive == b.positive,
                 .digits = std::move(output)};
}

ValueOrError<Decimal> AsDecimal(const Number& number, size_t decimal_digits) {
  return std::visit(
      [decimal_digits](const auto& value) -> ValueOrError<Decimal> {
        return AsDecimalBase(value, decimal_digits);
      },
      number);
}

std::wstring ToString(const Decimal& decimal, size_t decimal_digits) {
  std::wstring output;
  if (!decimal.positive) output.push_back(L'-');
  if (decimal_digits >= decimal.digits.value.size()) {
    output.push_back(L'0');
    if (decimal_digits > decimal.digits.value.size()) {
      output.push_back(L'.');
      for (size_t i = 0; i < decimal_digits - decimal.digits.value.size(); ++i)
        output.push_back(L'0');
    }
  }
  for (size_t i = 0; i < decimal.digits.value.size(); i++) {
    if (i == decimal.digits.value.size() - decimal_digits)
      output.push_back(L'.');
    output.push_back(L'0' +
                     decimal.digits.value[decimal.digits.value.size() - 1 - i]);
  }
  return output;
}

namespace {
const bool as_decimal_tests_registration =
    tests::Register(L"numbers::AsDecimal", [] {
      auto test = [](Number number, std::wstring expectation) {
        return tests::Test(
            {.name = expectation, .callback = [=] {
               std::wstring str = std::visit(
                   overload{[](Error error) { return error.read(); },
                            [](Decimal d) { return ToString(d, 2); }},
                   AsDecimal(number, 2));
               LOG(INFO) << "Representation: " << str;
               CHECK(str == expectation);
             }});
      };
      return std::vector(
          {test(45, L"45.00"),
           test(0, L"0.00"),
           test(-328, L"-328.00"),
           test(Addition{MakeNonNullShared<Number>(1),
                         MakeNonNullShared<Number>(0)},
                L"1.00"),
           test(Addition{MakeNonNullShared<Number>(Number(7)),
                         MakeNonNullShared<Number>(5)},
                L"12.00"),
           test(Addition{MakeNonNullShared<Number>(Number(7)),
                         MakeNonNullShared<Number>(-5)},
                L"2.00"),
           test(Addition{MakeNonNullShared<Number>(Number(7)),
                         MakeNonNullShared<Number>(-30)},
                L"-23.00"),
           test(Addition{MakeNonNullShared<Number>(Number(-7)),
                         MakeNonNullShared<Number>(-30)},
                L"-37.00"),
           test(Addition{MakeNonNullShared<Number>(Number(-100)),
                         MakeNonNullShared<Number>(30)},
                L"-70.00"),
           test(Addition{MakeNonNullShared<Number>(2147483647),
                         MakeNonNullShared<Number>(2147483647)},
                L"4294967294.00"),
           test(Multiplication{MakeNonNullShared<Number>(1),
                               MakeNonNullShared<Number>(10)},
                L"10.00"),
           test(Multiplication{MakeNonNullShared<Number>(-2),
                               MakeNonNullShared<Number>(25)},
                L"-50.00"),
           test(Multiplication{MakeNonNullShared<Number>(-1),
                               MakeNonNullShared<Number>(-35)},
                L"35.00"),
           test(Multiplication{MakeNonNullShared<Number>(11),
                               MakeNonNullShared<Number>(12)},
                L"132.00"),
           test(Multiplication{MakeNonNullShared<Number>(-1),
                               MakeNonNullShared<Number>(
                                   Addition{MakeNonNullShared<Number>(2),
                                            MakeNonNullShared<Number>(3)})},
                L"-5.00"),
           test(Multiplication{MakeNonNullShared<Number>(2147483647),
                               MakeNonNullShared<Number>(2147483647)},
                L"4611686014132420609.00"),
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
                L"0.01")});
    }());

}  // namespace
};  // namespace afc::language::numbers
