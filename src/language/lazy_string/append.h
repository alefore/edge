#ifndef __AFC_LANGUAGE_LAZY_STRING_APPEND_H__
#define __AFC_LANGUAGE_LAZY_STRING_APPEND_H__

#include <memory>
#include <ranges>
#include <vector>

#include "src/language/container.h"
#include "src/language/lazy_string/lazy_string.h"
#include "src/language/safe_types.h"

namespace afc::language::lazy_string {
// TODO(trivial, 2023-12-29): Remove these functions; replace with methods in
// LazyString.
LazyString Append(LazyString a, LazyString b);
LazyString Append(LazyString a, LazyString b, LazyString c);
LazyString Append(LazyString a, LazyString b, LazyString c, LazyString d);

template <std::ranges::range R>
LazyString Concatenate(R&& inputs) {
  // TODO: There's probably a faster way to do this. Not sure it matters.
  return container::Fold(
      [](LazyString fragment, LazyString total) {
        return std::move(total).Append(fragment);
      },
      EmptyString(), inputs);
}

// Returns a range transformation that can be used to intersperse a given
// LazyString between elements in a range of LazyString elements.
//
// For example:
//
//     std::vector<LazyString inputs = ...;
//     LazyString output =
//         Concatenate(inputs | Intersperse(NewLazyString(L", ")))
inline auto Intersperse(LazyString separator) {
  return std::views::transform([&](LazyString v) {
           return std::vector<LazyString>{separator, std::move(v)};
         }) |
         std::views::join |
         // Remove the first element (", ").
         std::views::drop(1);
}
}  // namespace afc::language::lazy_string

#endif  // __AFC_LANGUAGE_LAZY_STRING_APPEND_H__
