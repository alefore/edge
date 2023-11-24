#include "src/math/numbers.h"

#include <glog/logging.h>

#include <limits>
#include <ranges>

#include "src/language/container.h"
#include "src/language/error/value_or_error.h"
#include "src/language/overload.h"
#include "src/language/safe_types.h"
#include "src/language/wstring.h"
#include "src/math/checked_operation.h"
#include "src/tests/tests.h"

namespace container = afc::language::container;

using afc::language::Error;
using afc::language::MakeNonNullShared;
using afc::language::overload;
using afc::language::ValueOrError;
using ::operator<<;

namespace afc::math::numbers {
struct BinaryOperation {
  Number a;
  Number b;
};

struct Negation {
  Number a;
};

struct Addition : public BinaryOperation {};
struct Multiplication : public BinaryOperation {};
struct Division : public BinaryOperation {};

struct OperationTree {
  std::variant<int64_t, Addition, Negation, Multiplication, Division> variant;
};

namespace {
// Least significative digit first. The most significant digit (last digit) must
// not be 0. Zero should always be represented as the empty vector (never as
// {0}).
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

ValueOrError<Decimal> OperationTreeToDecimal(int64_t value,
                                             size_t decimal_digits) {
  Decimal output{.positive = value >= 0,
                 .digits = {Digits(std::vector<size_t>(decimal_digits, 0))}};
  while (value != 0) {
    output.digits.push_back(output.positive ? value % 10 : -(value % 10));
    value /= 10;
  }
  output.digits = RemoveSignificantZeros(output.digits);
  return output;
}

const bool operation_tree_to_decimal_int_tests_registration =
    tests::Register(L"numbers::OperationTreeToDecimalInt", [] {
      auto test = [](std::wstring name, int input, bool positive_expectation,
                     std::string expectation) {
        return tests::Test{
            .name = name,
            .callback = [input, positive_expectation, expectation] {
              Decimal output = ValueOrDie(OperationTreeToDecimal(input, 0));
              CHECK_EQ(output.positive, positive_expectation);
              std::string output_str = container::Map(
                  [](size_t d) { return '0' + d; },
                  output.digits | std::views::reverse, std::string{});
              CHECK_EQ(output_str, expectation);
            }};
      };
      return std::vector<tests::Test>({
          test(L"Zero", 0, true, ""),
          test(L"SimplePositive", 871, true, "871"),
          test(L"SimpleNegative", -6239, false, "6239"),
          test(L"Min", std::numeric_limits<int>::min(), false, "2147483648"),
          test(L"Max", std::numeric_limits<int>::max(), true, "2147483647"),
      });
    }());

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

const bool remove_decimals_tests_registration =
    tests::Register(L"numbers::RemoveDecimals", [] {
      auto test = [](std::wstring input, size_t digits,
                     std::wstring expectation) {
        return tests::Test(
            {.name = input, .callback = [=] {
               Digits input_digits =
                   container::Map([](wchar_t c) { return c - L'0'; },
                                  input | std::views::reverse, Digits());
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
  if (a.positive != b.positive) return b.positive;
  return a.positive ? a.digits < b.digits : b.digits < a.digits;
}

struct DivisionOutput {
  Digits digits;
  bool exact;
};
ValueOrError<DivisionOutput> DivideDigits(const Digits& dividend,
                                          const Digits& divisor,
                                          size_t extra_precision) {
  if (divisor.empty()) return Error(L"Division by zero.");
  Digits quotient;
  Digits current_dividend;
  for (size_t i = 0; i < dividend.size() + extra_precision; ++i) {
    size_t next = i < dividend.size() ? dividend[dividend.size() - 1 - i] : 0;
    if (!current_dividend.empty() || next != 0)
      current_dividend.insert(current_dividend.begin(), next);
    size_t x = 0;  // Largest number such that divisor * x <= current_dividend.
    while (divisor * Digits({x + 1}) <= current_dividend) ++x;
    CHECK_LE(x, 9ul);
    if (x > 0) current_dividend = current_dividend - divisor * Digits({x});
    quotient.insert(quotient.begin(), std::move(x));
  }
  return DivisionOutput{.digits = RemoveSignificantZeros(quotient),
                        .exact = current_dividend.empty()};
}

ValueOrError<Decimal> ToDecimal(const Number& number, size_t decimal_digits);

ValueOrError<Decimal> OperationTreeToDecimal(Addition value,
                                             size_t decimal_digits) {
  ASSIGN_OR_RETURN(Decimal a, ToDecimal(value.a, decimal_digits + 1));
  ASSIGN_OR_RETURN(Decimal b, ToDecimal(value.b, decimal_digits + 1));
  if (a.positive == b.positive)
    return Decimal{.positive = a.positive,
                   .exact = a.exact && b.exact,
                   .digits = RemoveDecimals(a.digits + b.digits, 1)};
  else if (a.digits >= b.digits)
    return Decimal{.positive = a.positive,
                   .exact = a.exact && b.exact,
                   .digits = RemoveDecimals(a.digits - b.digits, 1)};
  else
    return Decimal{.positive = b.positive,
                   .exact = a.exact && b.exact,
                   .digits = RemoveDecimals(b.digits - a.digits, 1)};
}

ValueOrError<Decimal> OperationTreeToDecimal(Negation value,
                                             size_t decimal_digits) {
  ASSIGN_OR_RETURN(Decimal output, ToDecimal(value.a, decimal_digits));
  output.positive = output.digits.empty() || !output.positive;
  return output;
}

ValueOrError<Decimal> OperationTreeToDecimal(Multiplication value,
                                             size_t decimal_digits) {
  // TODO(2023-09-21): This can be optimized to compute fewer decimal digits
  // in the recursions.
  ASSIGN_OR_RETURN(Decimal a, ToDecimal(value.a, decimal_digits));
  ASSIGN_OR_RETURN(Decimal b, ToDecimal(value.b, decimal_digits));
  Digits output_digits = std::move(a.digits) * std::move(b.digits);
  return Decimal{
      .positive = a.positive == b.positive || output_digits.empty(),
      .exact = a.exact && b.exact,
      .digits = RemoveDecimals(std::move(output_digits), decimal_digits)};
}

ValueOrError<Decimal> OperationTreeToDecimal(Division value,
                                             size_t decimal_digits) {
  ASSIGN_OR_RETURN(Decimal a, ToDecimal(value.a, decimal_digits));
  ASSIGN_OR_RETURN(Decimal b, ToDecimal(value.b, decimal_digits));
  ASSIGN_OR_RETURN(DivisionOutput output,
                   DivideDigits(a.digits, b.digits, decimal_digits));
  return Decimal{.positive = a.positive == b.positive || output.digits.empty(),
                 .exact = a.exact && b.exact && output.exact,
                 .digits = std::move(output.digits)};
}

ValueOrError<Decimal> ToDecimal(const Number& number, size_t decimal_digits) {
  return std::visit(
      [decimal_digits](const auto& value) -> ValueOrError<Decimal> {
        ASSIGN_OR_RETURN(Decimal output,
                         OperationTreeToDecimal(value, decimal_digits));
        CHECK(!output.digits.empty() || output.positive);
        CHECK(output.digits.empty() || output.digits.back() != 0);
        return output;
      },
      number.value.value().variant);
}

const bool as_decimal_tests_registration =
    tests::Register(L"numbers::ToDecimal", [] {
      auto test = [](Number number, std::wstring expectation,
                     std::wstring name = L"") {
        return tests::Test(
            {.name = name.empty() ? expectation : name, .callback = [=] {
               std::wstring str = std::visit(
                   overload{[](Error error) { return error.read(); },
                            [](Decimal d) { return ToString(d, 2); }},
                   ToDecimal(number, 2));
               LOG(INFO) << "Representation: " << str;
               CHECK(str == expectation);
             }});
      };
      return std::vector(
          {test(FromInt(45), L"45"), test(FromInt(-328), L"-328"),
           // Testing boundaries of int and numbers near them.
           test(FromInt(std::numeric_limits<int64_t>::min()),
                L"-9223372036854775808", L"MinInt64"),
           test(FromInt(std::numeric_limits<int64_t>::max()),
                L"9223372036854775807", L"MaxInt64"),
           // Testing numbers close to boundaries.
           test(FromInt(std::numeric_limits<int64_t>::max()) + FromInt(1),
                L"9223372036854775808", L"OverflowAddition"),
           test(FromInt(std::numeric_limits<int64_t>::min()) - FromInt(1),
                L"-9223372036854775809", L"UnderflowSubtraction"),
           test(FromInt(std::numeric_limits<int64_t>::max()) * FromInt(2),
                L"18446744073709551614", L"OverflowMultiplication"),
           test(FromInt(std::numeric_limits<int64_t>::min()) * FromInt(2),
                L"-18446744073709551616", L"UnderflowMultiplication"),

           // Other tests.
           test(FromInt(1) / FromDouble(0.01), L"100.00",
                L"DivisionByVerySmall"),
           test(-FromInt(0), L"0", L"ZeroNegation"),
           test(FromInt(5) - FromInt(5), L"0"),
           test(FromInt(1) + FromInt(0), L"1"),
           test(FromInt(7) + FromInt(5), L"12"),
           test(FromInt(7) + FromInt(-5), L"2"),
           test(FromInt(7) + FromInt(-30), L"-23"),
           test(FromInt(-7) + FromInt(-30), L"-37"),
           test(FromInt(-100) + FromInt(30), L"-70"),
           test(FromInt(2147483647) + FromInt(2147483647), L"4294967294"),
           test(FromInt(1) * FromInt(10), L"10"),
           test(FromInt(-5) * FromInt(0), L"0", L"X*0."),
           test(FromInt(0) * FromInt(-100), L"0", L"0*X."),
           test(FromInt(-2) * FromInt(25), L"-50"),
           test(FromInt(-1) * FromInt(-35), L"35"),
           test(FromInt(11) * FromInt(12), L"132"),
           test(FromInt(-1) * (FromInt(2) + FromInt(3)), L"-5"),
           test(FromInt(2147483647) * FromInt(2147483647),
                L"4611686014132420609"),
           test(FromInt(2147483647) * FromInt(2147483647) +
                    FromInt(3) / FromInt(100),
                L"4611686014132420609.03"),
           test(FromInt(0) / FromInt(-10), L"0", L"Division.0ByNegative"),
           test(FromInt(3) / FromInt(10), L"0.3"),
           test(FromInt(949949) / FromInt(1), L"949949"),
           test(FromInt(20) * FromInt(20) + FromInt(3) / FromInt(100),
                L"400.03"),
           test(FromInt(1) / FromInt(3), L"0.33"),
           test(FromInt(1) / FromInt(300) + FromInt(1) / FromInt(300), L"0.01"),
           test(FromInt(1) / FromInt(0), L"Division by zero.")});
    }());
}  // namespace

ValueOrError<std::wstring> ToString(const Number& number,
                                    size_t decimal_digits) {
  ASSIGN_OR_RETURN(Decimal decimal, ToDecimal(number, decimal_digits));
  return ToString(decimal, decimal_digits);
}

Number FromInt(int64_t value) {
  return Number{.value = MakeNonNullShared<OperationTree>(
                    OperationTree{.variant = value})};
}

ValueOrError<int32_t> ToInt32(const Number& number) {
  ASSIGN_OR_RETURN(int64_t value, ToInt(number));
  if (value < std::numeric_limits<int>::min() ||
      value > std::numeric_limits<int>::max())
    return Error(
        L"Overflow: the resulting number can't be represented as `int32_t`");
  return static_cast<int32_t>(value);
}

ValueOrError<int64_t> ToInt(const Number& number) {
  ASSIGN_OR_RETURN(Decimal decimal, ToDecimal(number, 0));
  if (!decimal.exact)
    return Error(L"Inexact numbers can't be represented as integer.");
  int64_t value = 0;
  for (size_t digit : decimal.digits | std::views::reverse) {
    ASSIGN_OR_RETURN(value, CheckedMultiply(value, 10));
    ASSIGN_OR_RETURN(
        value,
        CheckedAdd(value, static_cast<int>(decimal.positive ? digit : -digit)));
  }
  return value;
}

namespace {
const bool int_tests_registration = tests::Register(
    L"numbers::Int",
    {{.name = L"ToIntZero",
      .callback = [] { CHECK_EQ(ValueOrDie(ToInt(FromInt(0))), 0); }},
     {.name = L"ToIntSmallPositive",
      .callback = [] { CHECK_EQ(ValueOrDie(ToInt(FromInt(1024))), 1024); }},
     {.name = L"ToIntSmallNegative",
      .callback = [] { CHECK_EQ(ValueOrDie(ToInt(FromInt(-249))), -249); }},
     {.name = L"ToIntPositiveLimit",
      .callback =
          [] { CHECK_EQ(ValueOrDie(ToInt(FromInt(2147483647))), 2147483647); }},
     {.name = L"ToIntNegativeLimit",
      .callback =
          [] {
            int64_t input = -2147483648;
            int64_t value = ValueOrDie(ToInt(FromInt(input) + FromInt(0)));
            CHECK_EQ(value, input);
          }},
     {.name = L"OverflowPositive",
      .callback =
          [] {
            int64_t input = 9223372036854775807;
            CHECK(std::get<Error>(ToInt(FromInt(input) + FromInt(1)))
                      .read()
                      .substr(0, 10) == L"Overflow: ");
          }},
     {.name = L"OverflowNegative", .callback = [] {
        int64_t input = -9223372036854775807 - 1;
        CHECK(std::get<Error>(ToInt(FromInt(input) - FromInt(1)))
                  .read()
                  .substr(0, 10) == L"Overflow: ");
      }}});
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

Number Pow(Number base, size_t i) {
  Number output = FromInt(1);
  while (i > 0) {
    if (i % 2 == 1) {
      output = output * base;
      --i;
    } else {
      base *= base;
      i /= 2;
    }
  }
  return output;
}

Number FromDouble(double value) {
  union DoubleIntUnion {
    double dvalue;
    int64_t ivalue;
  };
  DoubleIntUnion du;
  du.dvalue = value;
  int64_t bits = du.ivalue;
  Number sign = ((bits >> 63) == 0) ? FromInt(1) : FromInt(-1);
  int64_t exponent = ((bits >> 52) & 0x7FFL) - 1023;
  Number mantissa = FromInt((bits & ((1LL << 52) - 1)) | (1LL << 52));
  Number numerator, denominator;

  // TODO(2023-10-06): Handle subnormal numbers, where exponent is -1023.
  if (exponent > 0) {
    numerator = sign * (mantissa * Pow(FromInt(2), exponent));
    denominator = Pow(FromInt(2), 52);
  } else {
    numerator = sign * mantissa;
    denominator = Pow(FromInt(2), 52 - exponent);
  }
  return numerator / denominator;
}

namespace {
const bool double_tests_registration = tests::Register(
    L"numbers::Double",
    {{.name = L"FromDouble",
      .callback =
          [] {
            auto str = ToString(FromDouble(5), 2);
            LOG(INFO) << "Representation: " << str;
            CHECK(ValueOrDie(std::move(str)) == L"5");
          }},
     {.name = L"FromDoubleSmall",
      .callback =
          [] {
            auto str = ToString(FromDouble(0.00000001), 8);
            LOG(INFO) << "Representation: " << str;
            CHECK(ValueOrDie(std::move(str)) == L"0.00000001");
          }},
     {.name = L"ToDoubleFromInt",
      .callback =
          [] { CHECK_NEAR(ValueOrDie(ToDouble(FromInt(5))), 5.0, 0.00001); }},
     {.name = L"ToDoubleFromDouble", .callback = [] {
        CHECK_NEAR(ValueOrDie(ToDouble(FromDouble(5))), 5.0, 0.00001);
      }}});
}

Number FromSizeT(size_t value) {
  const int base = 65536;  // 2^16
  Number result = FromInt(0);
  Number multiplier = FromInt(1);
  while (value != 0) {
    int chunk = value % base;
    result = std::move(result) + FromInt(chunk) * multiplier;
    value /= base;
    multiplier = std::move(multiplier) * FromInt(base);
  }
  return result;
}

ValueOrError<size_t> ToSizeT(const Number& number) {
  ASSIGN_OR_RETURN(Decimal decimal, ToDecimal(number, 0));
  if (!decimal.exact)
    return Error(L"Inexact numbers can't be represented as size_t.");
  if (!decimal.positive)
    return Error(L"Negative numbers can't be represented as size_t.");
  size_t value = 0;
  for (size_t digit : decimal.digits | std::views::reverse) {
    ASSIGN_OR_RETURN(value, CheckedMultiply<size_t>(value, 10));
    ASSIGN_OR_RETURN(value, CheckedAdd(value, digit));
  }
  return value;
}

namespace {
const bool size_t_tests_registration = tests::Register(
    L"numbers::SizeT",
    {{.name = L"FromSizeTSimple",
      .callback =
          [] {
            auto str = ToString(FromSizeT(5), 2);
            LOG(INFO) << "Representation: " << str;
            CHECK(ValueOrDie(std::move(str)) == L"5");
          }},
     {.name = L"FromSizeTMax",
      .callback =
          [] {
            auto str =
                ToString(FromSizeT(std::numeric_limits<size_t>::max()), 2);
            LOG(INFO) << "Representation: " << str;
            CHECK(ValueOrDie(std::move(str)) == L"18446744073709551615");
          }},
     {.name = L"ToSizeTSimple",
      .callback = [] { CHECK_EQ(ValueOrDie(ToSizeT(FromSizeT(5))), 5ul); }},
     {.name = L"ToSizeTMax",
      .callback =
          [] {
            CHECK_EQ(ValueOrDie(ToSizeT(
                         FromSizeT(std::numeric_limits<size_t>::max()))),
                     18446744073709551615ul);
          }},
     {.name = L"ToSizeTOverflow",
      .callback =
          [] {
            CHECK(std::get<Error>(
                      ToSizeT(FromInt(1) +
                              FromSizeT(std::numeric_limits<size_t>::max())))
                      .read()
                      .substr(0, 10) == L"Overflow: ");
          }},
     {.name = L"ToSizeTNegative",
      .callback =
          [] {
            CHECK(std::get<Error>(ToSizeT(FromInt(-1))).read().substr(0, 9) ==
                  L"Negative ");
          }},
     {.name = L"ToSizeTInexact", .callback = [] {
        ValueOrError<size_t> value = ToSizeT(FromDouble(1.5));
        LOG(INFO) << "Output: " << value;
        CHECK(std::get<Error>(value).read().substr(0, 8) == L"Inexact ");
      }}});
}  // namespace

ValueOrError<bool> IsEqual(const Number& a, const Number& b, size_t precision) {
  if (const int64_t* a_as_int64 = std::get_if<int64_t>(&a.value->variant);
      a_as_int64 != nullptr)
    if (const int64_t* b_as_int64 = std::get_if<int64_t>(&b.value->variant);
        b_as_int64 != nullptr)
      return *a_as_int64 == *b_as_int64;
  ASSIGN_OR_RETURN(Decimal a_decimal, ToDecimal(a, precision));
  ASSIGN_OR_RETURN(Decimal b_decimal, ToDecimal(b, precision));
  return a_decimal == b_decimal;
}

ValueOrError<bool> IsLessThan(const Number& a, const Number& b,
                              size_t precision) {
  if (const int64_t* a_as_int64 = std::get_if<int64_t>(&a.value->variant);
      a_as_int64 != nullptr)
    if (const int64_t* b_as_int64 = std::get_if<int64_t>(&b.value->variant);
        b_as_int64 != nullptr)
      return *a_as_int64 < *b_as_int64;
  ASSIGN_OR_RETURN(Decimal a_decimal, ToDecimal(a, precision));
  ASSIGN_OR_RETURN(Decimal b_decimal, ToDecimal(b, precision));
  return a_decimal < b_decimal;
}

ValueOrError<bool> IsLessThanOrEqual(const Number& a, const Number& b,
                                     size_t precision) {
  if (const int64_t* a_as_int64 = std::get_if<int64_t>(&a.value->variant);
      a_as_int64 != nullptr)
    if (const int64_t* b_as_int64 = std::get_if<int64_t>(&b.value->variant);
        b_as_int64 != nullptr)
      return *a_as_int64 <= *b_as_int64;
  ASSIGN_OR_RETURN(Decimal a_decimal, ToDecimal(a, precision));
  ASSIGN_OR_RETURN(Decimal b_decimal, ToDecimal(b, precision));
  return a_decimal < b_decimal || a_decimal == b_decimal;
}

Number& Number::operator+=(Number rhs) {
  if (const int64_t* this_as_int64 = std::get_if<int64_t>(&value->variant);
      this_as_int64 != nullptr)
    if (const int64_t* rhs_as_int64 = std::get_if<int64_t>(&rhs.value->variant);
        rhs_as_int64 != nullptr)
      if (ValueOrError<int64_t> output =
              CheckedAdd(*this_as_int64, *rhs_as_int64);
          std::holds_alternative<int64_t>(output)) {
        value = FromInt(std::get<int64_t>(output)).value;
        return *this;
      }
  value = MakeNonNullShared<OperationTree>(
      OperationTree{.variant = Addition{{{std::move(value)}, std::move(rhs)}}});
  return *this;
}

Number& Number::operator-=(Number rhs) { return operator+=(-rhs); }

Number& Number::operator*=(Number rhs) {
  if (const int64_t* this_as_int64 = std::get_if<int64_t>(&value->variant);
      this_as_int64 != nullptr)
    if (const int64_t* rhs_as_int64 = std::get_if<int64_t>(&rhs.value->variant);
        rhs_as_int64 != nullptr)
      if (ValueOrError<int64_t> output =
              CheckedMultiply(*this_as_int64, *rhs_as_int64);
          std::holds_alternative<int64_t>(output)) {
        value = FromInt(std::get<int64_t>(output)).value;
        return *this;
      }
  value = MakeNonNullShared<OperationTree>(OperationTree{
      .variant = Multiplication{{{std::move(value)}, std::move(rhs)}}});
  return *this;
}

Number& Number::operator/=(Number rhs) {
  value = MakeNonNullShared<OperationTree>(
      OperationTree{.variant = Division{{{std::move(value)}, std::move(rhs)}}});
  return *this;
}

Number operator+(Number a, Number b) {
  a += std::move(b);
  return a;
}

Number operator-(Number a, Number b) {
  a -= std::move(b);
  return a;
}

Number operator*(Number a, Number b) {
  a *= std::move(b);
  return a;
}

Number operator/(Number a, Number b) {
  a /= std::move(b);
  return a;
}

Number operator-(Number a) {
  if (const int64_t* a_as_int64 = std::get_if<int64_t>(&a.value->variant);
      a_as_int64 != nullptr &&
      *a_as_int64 != std::numeric_limits<int64_t>::min())
    return FromInt(-*a_as_int64);
  return Number{MakeNonNullShared<OperationTree>(
      OperationTree{.variant = Negation{std::move(a)}})};
}
};  // namespace afc::math::numbers
