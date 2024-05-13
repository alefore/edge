#ifndef __AFC_LANGUAGE_ERROR_VIEW_H__
#define __AFC_LANGUAGE_ERROR_VIEW_H__

#include "src/language/container.h"
#include "src/language/error/value_or_error.h"

namespace afc::language::view {

// View that removes errors from a range of `ValueOrError<T>` instances and
// unwraps the values (into just `T`).
inline constexpr auto SkipErrors =
    std::views::filter([](const auto& v) { return !IsError(v); }) |
    std::views::transform([](auto v) { return ValueOrDie(std::move(v)); });

inline constexpr auto GetErrors =
    std::views::filter([](const auto& v) { return IsError(v); }) |
    std::views::transform([](auto& v) { return std::get<Error>(v); });

template <typename T>
ValueOrError<std::vector<T>> ExtractErrors(std::vector<ValueOrError<T>> input) {
  if (std::vector<Error> errors =
          container::MaterializeVector(input | GetErrors);
      !errors.empty())
    return MergeErrors(errors, L", ");
  return container::MaterializeVector(input | SkipErrors);
}

}  // namespace afc::language::view

#endif  // __AFC_LANGUAGE_ERROR_VIEW_H__
