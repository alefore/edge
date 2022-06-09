#include "src/language/lazy_string/trim.h"

#include <glog/logging.h>

#include "src/language/lazy_string/functional.h"
#include "src/language/lazy_string/substring.h"

namespace afc::language::lazy_string {
// TODO(easy, 2022-06-09): Drop `String` from the prefix? It's implied by the
// namespace?
NonNull<std::shared_ptr<LazyString>> StringTrimLeft(
    NonNull<std::shared_ptr<LazyString>> source,
    std::wstring space_characters) {
  // TODO(easy, 2022-06-08): Add a tracker to see if we're spending too much
  // time here. This could be optimized by turning space_characters into a set.
  return Substring(
      source, FindFirstColumnWithPredicate(source.value(), [&](ColumnNumber,
                                                               wchar_t c) {
                return space_characters.find(c) == std::wstring::npos;
              }).value_or(ColumnNumber(0) + source->size()));
}

}  // namespace afc::language::lazy_string