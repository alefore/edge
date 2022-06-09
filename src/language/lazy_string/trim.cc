#include "src/language/lazy_string/trim.h"

#include <glog/logging.h>

#include "src/infrastructure/tracker.h"
#include "src/language/lazy_string/functional.h"
#include "src/language/lazy_string/substring.h"

namespace afc::language::lazy_string {
using infrastructure::Tracker;

NonNull<std::shared_ptr<LazyString>> TrimLeft(
    NonNull<std::shared_ptr<LazyString>> source,
    std::wstring space_characters) {
  static Tracker tracker(L"LazyString::StringTrimLeft");
  auto call = tracker.Call();
  return Substring(
      source, FindFirstColumnWithPredicate(source.value(), [&](ColumnNumber,
                                                               wchar_t c) {
                return space_characters.find(c) == std::wstring::npos;
              }).value_or(ColumnNumber(0) + source->size()));
}

}  // namespace afc::language::lazy_string
