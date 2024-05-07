#include "src/math/numbers.h"

#include <glog/logging.h>

#include <limits>
#include <ranges>

#include "src/language/container.h"
#include "src/language/error/value_or_error.h"
#include "src/language/overload.h"
#include "src/language/safe_types.h"
#include "src/language/wstring.h"
#include "src/math/bigint.h"
#include "src/math/checked_operation.h"
#include "src/tests/tests.h"

namespace container = afc::language::container;

using afc::language::Error;
using afc::language::MakeNonNullShared;
using afc::language::NewError;
using afc::language::overload;
using afc::language::ValueOrDie;
using afc::language::ValueOrError;
using afc::language::lazy_string::LazyString;

namespace afc::math::numbers {
using ::operator<<;

Number Number::operator+(Number other) && {
  if (!positive_ && !other.positive_)
    return (std::move(*this).Negate() + std::move(other).Negate()).Negate();
  if (!positive_) return std::move(other) - std::move(*this).Negate();
  if (!other.positive_) return std::move(*this) - std::move(other).Negate();

  BigInt new_numerator = std::move(numerator_) * other.denominator_.value() +
                         denominator_.value() * std::move(other.numerator_);
  NonZeroBigInt new_denominator =
      std::move(denominator_) * std::move(other.denominator_);
  return Number(true, std::move(new_numerator), std::move(new_denominator));
}

Number Number::operator-(Number other) && {
  if (!positive_ && !other.positive_)
    return (std::move(*this).Negate() - std::move(other).Negate()).Negate();
  if (!positive_)
    return (std::move(*this).Negate() + std::move(other)).Negate();
  if (!other.positive_) return std::move(*this) + std::move(other);

  BigInt a = std::move(numerator_) * other.denominator_.value();
  BigInt b = std::move(other.numerator_) * denominator_.value();
  BigInt new_numerator = ValueOrDie(a >= b ? std::move(a) - std::move(b)
                                           : std::move(b) - std::move(a));
  NonZeroBigInt new_denominator =
      std::move(denominator_) * std::move(other.denominator_);
  return Number(a >= b, std::move(new_numerator), std::move(new_denominator));
}

Number Number::operator*(Number other) && {
  BigInt new_numerator = std::move(numerator_) * std::move(other.numerator_);
  NonZeroBigInt new_denominator =
      std::move(denominator_) * std::move(other.denominator_);
  return Number(positive_ == other.positive_, std::move(new_numerator),
                std::move(new_denominator));
}

ValueOrError<Number> Number::operator/(Number other) && {
  DECLARE_OR_RETURN(Number reciprocal, std::move(other).Reciprocal());
  return std::move(*this) * std::move(reciprocal);
}

Number Number::Negate() && {
  return Number(!positive_, std::move(numerator_), std::move(denominator_));
}

ValueOrError<Number> Number::Reciprocal() && {
  return std::visit(
      overload{[](Error) -> ValueOrError<Number> {
                 return NewError(LazyString{L"Zero has no reciprocal."});
               },
               [&](NonZeroBigInt new_denominator) -> ValueOrError<Number> {
                 return Number{positive_, std::move(denominator_).value(),
                               std::move(new_denominator)};
               }},
      NonZeroBigInt::New(std::move(numerator_)));
}

void Number::Optimize() {
  std::visit(
      overload{[&](Error) {
                 CHECK(numerator_.IsZero());
                 denominator_ = NonZeroBigInt::Constant<1>();
               },
               [&](NonZeroBigInt non_zero_numerator) {
                 NonZeroBigInt gcd =
                     non_zero_numerator.GreatestCommonDivisor(denominator_);
                 {
                   BigIntDivideOutput divided_numerator =
                       Divide(std::move(non_zero_numerator).value(), gcd);
                   CHECK(divided_numerator.remainder.IsZero());
                   numerator_ = divided_numerator.quotient;
                 }
                 {
                   BigIntDivideOutput divided_denominator =
                       Divide(std::move(denominator_).value(), gcd);
                   CHECK(divided_denominator.remainder.IsZero());
                   // TODO(2024-04-09, P2): Find a way to use types to
                   // avoid this ValueOrDie:
                   denominator_ = ValueOrDie(
                       NonZeroBigInt::New(divided_denominator.quotient));
                 }
               }},
      NonZeroBigInt::New(std::move(numerator_)));
}

std::wstring Number::ToString(size_t maximum_decimal_digits) const {
  BigInt scaled_numerator =
      BigInt(numerator_) * BigInt::FromNumber(10).Pow(
                               BigInt::FromNumber(maximum_decimal_digits + 1));
  BigIntDivideOutput divide_output =
      Divide(std::move(scaled_numerator), denominator_);
  bool exact = divide_output.remainder.IsZero();

  // Rounding.
  divide_output =
      Divide(std::move(divide_output.quotient), NonZeroBigInt::Constant<10>());
  exact = exact && divide_output.remainder.IsZero();
  if (divide_output.remainder >= BigInt::FromNumber(5))
    ++divide_output.quotient;

  std::wstring output = divide_output.quotient.ToString();
  if (output.length() < maximum_decimal_digits)
    output =
        std::wstring(maximum_decimal_digits - output.length(), L'0') + output;
  if (maximum_decimal_digits > 0) {
    output.insert(output.length() - maximum_decimal_digits, L".");
    if (exact) {
      output.erase(output.find_last_not_of(L'0') + 1, std::wstring::npos);
      if (!output.empty() && output.back() == L'.') output.pop_back();
    }
  }
  if (output.empty() || output.front() == L'.')
    output.insert(output.begin(), L'0');
  if (!positive_) output.insert(output.begin(), L'-');
  return output;
}

const bool tostring_tests_registration =
    tests::Register(L"numbers::Number::ToString", [] {
      auto test = [](std::wstring name, Number input, size_t decimal_digits,
                     const std::wstring& expectation) {
        return tests::Test{
            .name = name, .callback = [input, decimal_digits, expectation] {
              std::wstring output = input.ToString(decimal_digits);
              CHECK(output == expectation)
                  << "Expected " << expectation << " but got " << output;
              ;
            }};
      };

      return std::vector<tests::Test>(
          {test(L"WholeNumber", Number::FromDouble(123.0), 0, L"123"),
           test(L"InexactNumberRoundedDown", Number::FromDouble(123.454), 2,
                L"123.45"),
           test(L"InexactNumberRoundedUp", Number::FromDouble(123.456), 2,
                L"123.46"),
           test(L"NegativeNumber", Number::FromInt64(-123), 2, L"-123"),
           test(L"Zero", Number::FromInt64(0), 2, L"0"),
           test(L"NegativeFractional",
                ValueOrDie(Number::FromInt64(-5) / Number::FromInt64(100)), 5,
                L"-0.05"),
           test(L"NegativeFractionalInexact",
                ValueOrDie(Number::FromInt64(-5) / Number::FromInt64(100)) +
                    Number::FromDouble(0.00000001),
                5, L"-0.05000")});
    }());

Number& Number::operator+=(Number rhs) {
  *this = std::move(*this) + std::move(rhs);
  return *this;
}

Number& Number::operator-=(Number rhs) {
  *this = std::move(*this) - std::move(rhs);
  return *this;
}

Number& Number::operator*=(Number rhs) {
  *this = std::move(*this) * std::move(rhs);
  return *this;
}

Number& Number::operator/=(Number rhs) {
  *this = ValueOrDie(std::move(*this) / std::move(rhs));
  return *this;
}

namespace {
const bool operator_divide_tests_registration = tests::Register(
    L"numbers::Number::operator/",
    {
        {.name = L"OldPrecisionFailure",
         .callback =
             [] {
               // This used to fail in an old implementation.
               CHECK((ValueOrDie(Number::FromInt64(1) / Number::FromInt64(3)) *
                      Number::FromInt64(1024))
                         .ToString(5) == L"341.33333");
             }},
    });
}  // namespace

/* static */ Number Number::FromBigInt(BigInt value) {
  return Number(true, std::move(value), NonZeroBigInt::Constant<1>());
}

/* static */ Number Number::FromInt64(int64_t value) {
  // We can't represent abs(std::numeric_limits<int64_t>::min()) as an
  // int64_t, so we handle this case explicitly.
  BigInt numerator = BigInt::FromNumber<uint64_t>(
      value == std::numeric_limits<int64_t>::min()
          ? static_cast<uint64_t>(-(value + 1)) + 1
          : static_cast<uint64_t>(std::abs(value)));
  static NonZeroBigInt denominator = NonZeroBigInt::Constant<1>();
  return Number(value >= 0, std::move(numerator), denominator);
}

afc::language::ValueOrError<int32_t> Number::ToInt32() const {
  BigInt quotient = Divide(numerator_, denominator_).quotient;
  DECLARE_OR_RETURN(int32_t abs_value, quotient.ToInt32());
  return CheckedMultiply<int32_t, int32_t>(abs_value, positive_ ? 1 : -1);
}

afc::language::ValueOrError<int64_t> Number::ToInt64() const {
  return Divide(numerator_, denominator_).quotient.ToInt64(positive_);
}

namespace {
const bool int_tests_registration = tests::Register(
    L"numbers::Number::Int",
    {{.name = L"ToIntZero",
      .callback =
          [] { CHECK_EQ(ValueOrDie(Number::FromInt64(0).ToInt64()), 0); }},
     {.name = L"ToIntSmallPositive",
      .callback =
          [] {
            CHECK_EQ(ValueOrDie(Number::FromInt64(1024).ToInt64()), 1024);
          }},
     {.name = L"ToIntSmallNegative",
      .callback =
          [] {
            CHECK_EQ(ValueOrDie(Number::FromInt64(-249).ToInt64()), -249);
          }},
     {.name = L"ToIntPositiveLimit",
      .callback =
          [] {
            int64_t input = 9223372036854775807;
            CHECK_EQ(ValueOrDie(Number::FromInt64(input).ToInt64()), input);
          }},
     {.name = L"ToIntNegativeLimit",
      .callback =
          [] {
            int64_t input = -9223372036854775807 - 1;
            int64_t value = ValueOrDie((Number::FromInt64(input)).ToInt64());
            CHECK_EQ(value, input);
          }},
     {.name = L"OverflowPositive",
      .callback =
          [] {
            int64_t input = 9223372036854775807;
            CHECK(
                std::get<Error>(
                    (Number::FromInt64(input) + Number::FromInt64(1)).ToInt64())
                    .read()
                    .substr(0, 10) == L"Overflow: ");
          }},
     {.name = L"OverflowNegative", .callback = [] {
        int64_t input = -9223372036854775807 - 1;
        CHECK(std::get<Error>(
                  (Number::FromInt64(input) - Number::FromInt64(1)).ToInt64())
                  .read()
                  .substr(0, 10) == L"Overflow: ");
      }}});
}  // namespace

afc::language::ValueOrError<size_t> Number::ToSizeT() const {
  // TODO(2024-04-09, P1, tests): Test that a negative number returns an error.
  return Divide(numerator_, denominator_).quotient.ToSizeT();
}

const bool to_size_t_tests_registration = tests::Register(
    L"numbers::ToSizeT",
    {{.name = L"Simple",
      .callback =
          [] { CHECK_EQ(ValueOrDie(Number::FromSizeT(5).ToSizeT()), 5ul); }},
     {.name = L"ToSizeTMax",
      .callback =
          [] {
            CHECK_EQ(
                ValueOrDie(Number::FromSizeT(std::numeric_limits<size_t>::max())
                               .ToSizeT()),
                18446744073709551615ul);
          }},
     {.name = L"Overflow",
      .callback =
          [] {
            CHECK(std::get<Error>(
                      (Number::FromInt64(1) +
                       Number::FromSizeT(std::numeric_limits<size_t>::max()))
                          .ToSizeT())
                      .read()
                      .substr(0, 10) == L"Overflow: ");
          }},
     {.name = L"Negative",
      .callback =
          [] {
            CHECK(std::get<Error>(Number::FromInt64(-1).ToSizeT())
                      .read()
                      .substr(0, 9) == L"Negative ");
          }},
     {.name = L"Inexact", .callback = [] {
        ValueOrError<size_t> value = Number::FromDouble(1.5).ToSizeT();
        LOG(INFO) << "Output: " << value;
        CHECK(std::get<Error>(value).read().substr(0, 8) == L"Inexact ");
      }}});

ValueOrError<double> Number::ToDouble() const {
  DECLARE_OR_RETURN(double numerator_double, numerator_.ToDouble());
  DECLARE_OR_RETURN(double denominator_double, denominator_.value().ToDouble());
  return numerator_double / denominator_double;
}

const bool to_double_tests_registration = tests::Register(
    L"numbers::ToDouble",
    {{.name = L"FromInt",
      .callback =
          [] {
            CHECK_NEAR(ValueOrDie(Number::FromInt64(5).ToDouble()), 5.0,
                       0.00001);
          }},
     {.name = L"FromDouble", .callback = [] {
        CHECK_NEAR(ValueOrDie(Number::FromDouble(5).ToDouble()), 5.0, 0.00001);
      }}});

Number Number::FromSizeT(size_t value) {
  const int base = 65536;  // 2^16
  Number result = FromInt64(0);
  Number multiplier = FromInt64(1);
  while (value != 0) {
    int chunk = value % base;
    result = std::move(result) + FromInt64(chunk) * multiplier;
    value /= base;
    multiplier = std::move(multiplier) * FromInt64(base);
  }
  return result;
}

const bool from_size_t_tests_registration = tests::Register(
    L"numbers::FromSizeT",
    {
        {.name = L"Simple",
         .callback =
             [] {
               std::wstring str = Number::FromSizeT(5).ToString(2);
               LOG(INFO) << "Representation: " << str;
               CHECK(std::move(str) == L"5");
             }},
        {.name = L"Max",
         .callback =
             [] {
               std::wstring str =
                   Number::FromSizeT(std::numeric_limits<size_t>::max())
                       .ToString(2);
               LOG(INFO) << "Representation: " << str;
               CHECK(std::move(str) == L"18446744073709551615");
             }},
    });

Number Number::FromDouble(double value) {
  union DoubleIntUnion {
    double dvalue;
    int64_t ivalue;
  };
  DoubleIntUnion du;
  du.dvalue = value;
  int64_t bits = du.ivalue;
  bool positive = (bits >> 63) == 0;
  int64_t exponent = ((bits >> 52) & 0x7FFL) - 1023;
  BigInt mantissa =
      BigInt::FromNumber((bits & ((1LL << 52) - 1)) | (1LL << 52));

  // TODO(2023-10-06): Handle subnormal numbers, where exponent is -1023.
  if (exponent > 0) {
    return Number(positive,
                  std::move(mantissa) *
                      BigInt::FromNumber(2).Pow(BigInt::FromNumber(exponent)),
                  NonZeroBigInt::Constant<2>().Pow(BigInt::FromNumber(52)));
  }

  return Number(
      positive, std::move(mantissa),
      NonZeroBigInt::Constant<2>().Pow(BigInt::FromNumber(52 - exponent)));
}

const bool from_double_tests_registration = tests::Register(
    L"numbers::FromDouble",
    {{.name = L"Five",
      .callback =
          [] {
            std::wstring str = Number::FromDouble(5).ToString(2);
            LOG(INFO) << "Representation: " << str;
            CHECK(std::move(str) == L"5");
          }},
     {.name = L"Small", .callback = [] {
        std::wstring str = Number::FromDouble(0.00000001).ToString(8);
        LOG(INFO) << "Representation: " << str;
        CHECK(std::move(str) == L"0.00000001");
      }}});

Number Number::Pow(BigInt exponent) && {
  return Number(positive_ || (exponent % NonZeroBigInt::Constant<2>()).IsZero(),
                std::move(numerator_).Pow(BigInt(exponent)),
                std::move(denominator_).Pow(BigInt(exponent)));
}

bool Number::operator==(const Number& other) const {
  return positive_ == other.positive_ &&
         numerator_ * other.denominator_.value() ==
             denominator_.value() * other.numerator_;
}

bool Number::operator>(const Number& other) const {
  if (!positive_ && !other.positive_)
    return Number{*this}.Negate() < Number{other}.Negate();
  if (!other.positive_) return true;
  if (!positive_) return false;
  return numerator_ * other.denominator_.value() >
         other.numerator_ * denominator_.value();
}

bool Number::operator<(const Number& other) const { return other > *this; }

bool Number::operator<=(const Number& other) const { return !(*this > other); }

bool Number::operator>=(const Number& other) const { return !(*this < other); }

namespace {
const bool comparison_tests_registration = tests::Register(
    L"numbers::Number::Comparison",
    {{.name = L"GreaterThanPositive",
      .callback =
          [] { CHECK(Number::FromInt64(1024) > Number::FromInt64(1023)); }},
     {.name = L"GreaterThanNegative",
      .callback =
          [] { CHECK(Number::FromInt64(-1023) > Number::FromInt64(-1024)); }},
     {.name = L"LessThanPositive",
      .callback =
          [] { CHECK(Number::FromInt64(231) < Number::FromInt64(232)); }},
     {.name = L"LessThanNegative",
      .callback = [] { CHECK(Number::FromInt64(-3) < Number::FromInt64(-2)); }},
     {.name = L"LessThanOrEqualToPositiveEqual",
      .callback =
          [] { CHECK(Number::FromInt64(1024) <= Number::FromInt64(1024)); }},
     {.name = L"LessThanOrEqualToPositiveLess",
      .callback =
          [] { CHECK(Number::FromInt64(1023) <= Number::FromInt64(1024)); }},
     {.name = L"GreaterThanOrEqualToNegativeEqual",
      .callback =
          [] { CHECK(Number::FromInt64(-1024) >= Number::FromInt64(-1024)); }},
     {.name = L"GreaterThanOrEqualToNegativeGreater",
      .callback =
          [] { CHECK(Number::FromInt64(-512) >= Number::FromInt64(-1024)); }},
     {.name = L"EqualToZero",
      .callback =
          [] {
            CHECK(!(Number::FromInt64(0) > Number::FromInt64(0)));
            CHECK(!(Number::FromInt64(0) < Number::FromInt64(0)));
            CHECK(Number::FromInt64(0) <= Number::FromInt64(0));
            CHECK(Number::FromInt64(0) >= Number::FromInt64(0));
          }},
     {.name = L"NegativeLessThanZero",
      .callback = [] { CHECK(Number::FromInt64(-1) < Number::FromInt64(0)); }},
     {.name = L"PositiveGreaterThanZero",
      .callback = [] { CHECK(Number::FromInt64(1) > Number::FromInt64(0)); }},
     {.name = L"EqualNumbers",
      .callback =
          [] {
            CHECK(!(Number::FromInt64(1024) > Number::FromInt64(1024)));
            CHECK(!(Number::FromInt64(1024) < Number::FromInt64(1024)));
            CHECK(Number::FromInt64(1024) <= Number::FromInt64(1024));
            CHECK(Number::FromInt64(1024) >= Number::FromInt64(1024));
          }},
     {.name = L"PositiveGreaterThanNegative",
      .callback = [] { CHECK(Number::FromInt64(1) > Number::FromInt64(-1)); }},
     {.name = L"NegativeLessThanPositive",
      .callback = [] { CHECK(Number::FromInt64(-1) < Number::FromInt64(1)); }},
     {.name = L"PositiveGreaterThanOrEqualToNegative",
      .callback =
          [] {
            CHECK(Number::FromInt64(1) >= Number::FromInt64(-1));
            CHECK(Number::FromInt64(-1) <= Number::FromInt64(1));
          }},
     {.name = L"NegativeLessThanOrEqualToPositive",
      .callback =
          [] {
            CHECK(Number::FromInt64(-1) <= Number::FromInt64(1));
            CHECK(Number::FromInt64(1) >= Number::FromInt64(-1));
          }},
     {.name = L"PositiveNotLessThanNegative",
      .callback =
          [] { CHECK(!(Number::FromInt64(1) < Number::FromInt64(-1))); }},
     {.name = L"NegativeNotGreaterThanPositive",
      .callback =
          [] { CHECK(!(Number::FromInt64(-1) > Number::FromInt64(1))); }},
     {.name = L"PositiveNotLessThanOrEqualToNegative",
      .callback =
          [] { CHECK(!(Number::FromInt64(1) <= Number::FromInt64(-1))); }},
     {.name = L"NegativeNotGreaterThanOrEqualToPositive", .callback = [] {
        CHECK(!(Number::FromInt64(-1) >= Number::FromInt64(1)));
      }}});
}  // namespace

}  // namespace afc::math::numbers

#if 0
namespace {
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

#endif
