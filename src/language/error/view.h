#ifndef __AFC_LANGUAGE_ERROR_VIEW_H__
#define __AFC_LANGUAGE_ERROR_VIEW_H__

#include "src/language/error/value_or_error.h"

namespace afc::language::view {

// View that removes errors from a range of `ValueOrError<T>` instances and
// unwraps the values (into just `T`).
inline constexpr auto SkipErrors =
    std::views::filter([](const auto& v) { return !IsError(v); }) |
    std::views::transform([](auto v) { return ValueOrDie(std::move(v)); });
}  // namespace afc::language::view

#endif  // __AFC_LANGUAGE_ERROR_VIEW_H__
