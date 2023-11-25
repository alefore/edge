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

}  // namespace afc::language::lazy_string

#endif  // __AFC_LANGUAGE_LAZY_STRING_APPEND_H__
