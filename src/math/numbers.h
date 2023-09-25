#ifndef __AFC_MATH_NUMBERS_H__
#define __AFC_MATH_NUMBERS_H__

#include <memory>
#include <string>
#include <variant>
#include <vector>

#include "src/language/error/value_or_error.h"

namespace afc::math::numbers {
struct Addition;
struct Negation;
struct Multiplication;
struct Division;

using Number = std::variant<int, Addition, Negation, Multiplication, Division>;
using NumberPtr = language::NonNull<std::shared_ptr<Number>>;
struct Addition {
  NumberPtr a;
  NumberPtr b;
};
struct Negation {
  NumberPtr a;
};
struct Multiplication {
  NumberPtr a;
  NumberPtr b;
};
struct Division {
  NumberPtr a;
  NumberPtr b;
};

afc::language::ValueOrError<std::wstring> ToString(const Number& number,
                                                   size_t decimal_digits);

afc::language::ValueOrError<int> ToInt(const Number& number);

afc::language::ValueOrError<double> ToDouble(const Number& number);
Number FromDouble(double);

Number FromSizeT(size_t);
afc::language::ValueOrError<size_t> ToSizeT(const Number& number);

afc::language::ValueOrError<bool> IsEqual(const Number& a, const Number& b,
                                          size_t precision);
afc::language::ValueOrError<bool> IsLessThan(const Number& a, const Number& b,
                                             size_t precision);
afc::language::ValueOrError<bool> IsLessThanOrEqual(const Number& a,
                                                    const Number& b,
                                                    size_t precision);
}  // namespace afc::math::numbers

#endif  // __AFC_MATH_NUMBERS_H__
