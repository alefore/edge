#ifndef __AFC_LANGUAGE_LAZY_STRING_APPEND_H__
#define __AFC_LANGUAGE_LAZY_STRING_APPEND_H__

#include <memory>
#include <ranges>
#include <vector>

#include "src/language/container.h"
#include "src/language/lazy_string/lazy_string.h"
#include "src/language/safe_types.h"

namespace afc::language::lazy_string {
language::NonNull<std::shared_ptr<LazyString>> Append(
    language::NonNull<std::shared_ptr<LazyString>> a,
    language::NonNull<std::shared_ptr<LazyString>> b);
language::NonNull<std::shared_ptr<LazyString>> Append(
    language::NonNull<std::shared_ptr<LazyString>> a,
    language::NonNull<std::shared_ptr<LazyString>> b,
    language::NonNull<std::shared_ptr<LazyString>> c);
language::NonNull<std::shared_ptr<LazyString>> Append(
    language::NonNull<std::shared_ptr<LazyString>> a,
    language::NonNull<std::shared_ptr<LazyString>> b,
    language::NonNull<std::shared_ptr<LazyString>> c,
    language::NonNull<std::shared_ptr<LazyString>> d);

template <std::ranges::range R>
language::NonNull<std::shared_ptr<LazyString>> Concatenate(R&& inputs) {
  // TODO: There's probably a faster way to do this. Not sure it matters.
  return container::Fold(
      [](language::NonNull<std::shared_ptr<LazyString>> fragment,
         language::NonNull<std::shared_ptr<LazyString>> total) {
        return Append(std::move(total), fragment);
      },
      EmptyString(), inputs);
}

// Returns a range transformation that can be used to intersperse a given
// LazyString between elements in a range of LazyString elements.
//
// For example:
//
//     std::vector<NonNull<std::shared_ptr<LazyString>> inputs = ...;
//     NonNull<std::shared_ptr<LazyString>> output =
//         Concatenate(inputs | Intersperse(NewLazyString(L", ")))
inline auto Intersperse(NonNull<std::shared_ptr<LazyString>> separator) {
  return std::views::transform([&](NonNull<std::shared_ptr<LazyString>> v) {
           return std::vector<NonNull<std::shared_ptr<LazyString>>>{
               separator, std::move(v)};
         }) |
         std::views::join |
         // Remove the first element (", ").
         std::views::drop(1);
}
}  // namespace afc::language::lazy_string

#endif  // __AFC_LANGUAGE_LAZY_STRING_APPEND_H__
