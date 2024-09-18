#ifndef __AFC_LANGUAGE_LAZY_STRING_APPEND_H__
#define __AFC_LANGUAGE_LAZY_STRING_APPEND_H__

#include <memory>
#include <ranges>
#include <vector>

#include "src/language/container.h"
#include "src/language/lazy_string/lazy_string.h"
#include "src/language/safe_types.h"

namespace afc::language::lazy_string {
template <std::ranges::range R>
auto Concatenate(R&& inputs) {
  using Type = typename std::remove_const<
      typename std::remove_reference<decltype(*inputs.begin())>::type>::type;
  return container::Fold(
      [](auto fragment, auto total) {
        return std::move(total) + std::move(fragment);
      },
      Type{}, inputs);
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
