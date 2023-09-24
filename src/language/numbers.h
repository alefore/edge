#ifndef __AFC_LANGUAGE_NUMBERS_H__
#define __AFC_LANGUAGE_NUMBERS_H__

#include <memory>
#include <string>
#include <variant>
#include <vector>

#include "src/language/error/value_or_error.h"

namespace afc::language::numbers {
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

ValueOrError<std::wstring> ToString(const Number& number,
                                    size_t decimal_digits);

ValueOrError<int> ToInt(const Number& number);

ValueOrError<double> ToDouble(const Number& number);
Number FromDouble(double);

Number FromSizeT(size_t);
ValueOrError<size_t> ToSizeT(const Number& number);

ValueOrError<bool> IsEqual(const Number& a, const Number& b, size_t precision);
ValueOrError<bool> IsLessThan(const Number& a, const Number& b,
                              size_t precision);
ValueOrError<bool> IsLessThanOrEqual(const Number& a, const Number& b,
                                     size_t precision);
}  // namespace afc::language::numbers

#endif  // __AFC_LANGUAGE_NUMBERS_H__
