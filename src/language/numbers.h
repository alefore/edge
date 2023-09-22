#ifndef __AFC_LANGUAGE_NUMBERS_H__
#define __AFC_LANGUAGE_NUMBERS_H__

#include <memory>
#include <string>
#include <variant>
#include <vector>

#include "src/language/error/value_or_error.h"

namespace afc::language::numbers {
struct Addition;
struct Multiplication;
struct Division;

using Number = std::variant<int, Addition, Multiplication, Division>;
using NumberPtr = language::NonNull<std::shared_ptr<Number>>;
struct Addition {
  NumberPtr a;
  NumberPtr b;
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
}  // namespace afc::language::numbers

#endif  // __AFC_LANGUAGE_NUMBERS_H__
