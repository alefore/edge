#ifndef __AFC_LANGUAGE_LAZY_STRING_APPEND_H__
#define __AFC_LANGUAGE_LAZY_STRING_APPEND_H__

#include <algorithm>
#include <memory>
#include <ranges>
#include <vector>

#include "src/language/lazy_string/lazy_string.h"
#include "src/language/safe_types.h"

namespace afc::language::lazy_string {
class SingleLine;
class NonEmptySingleLine;

template <std::ranges::range R>
auto Concatenate(R&& inputs) {
  using InputType = std::remove_cvref_t<std::ranges::range_reference_t<R>>;
  // The concatenation of non-empty lines … can still be empty (because the
  // sequence may itself be empty). So we short-circuit this case.
  using OutputType =
      std::conditional_t<std::is_same_v<InputType, NonEmptySingleLine>,
                         SingleLine, InputType>;
  return std::ranges::fold_left(inputs, OutputType{},
                                [](OutputType total, const auto& fragment) {
                                  return std::move(total) + fragment;
                                });
}

// Returns a range transformation that can be used to intersperse a given
// LazyString between elements in a range of LazyString elements.
//
// For example:
//
//     std::vector<LazyString> inputs = ...;
//     LazyString output =
//         Concatenate(inputs | Intersperse(LazyString{L", "}))
template <typename S>
auto Intersperse(S separator) {
  return std::views::transform(
             [&](S v) { return std::vector<S>{separator, std::move(v)}; }) |
         std::views::join |
         // Remove the first separator element.
         std::views::drop(1);
}
}  // namespace afc::language::lazy_string

#endif  // __AFC_LANGUAGE_LAZY_STRING_APPEND_H__
