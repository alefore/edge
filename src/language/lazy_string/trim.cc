#include "src/language/lazy_string/trim.h"

#include <glog/logging.h>

#include "src/infrastructure/tracker.h"
#include "src/language/lazy_string/functional.h"

namespace afc::language::lazy_string {
using infrastructure::Tracker;

LazyString TrimLeft(LazyString source, std::wstring space_characters) {
  static Tracker tracker(L"LazyString::StringTrimLeft");
  auto call = tracker.Call();
  return source.Substring(
      FindFirstColumnWithPredicate(source, [&](ColumnNumber, wchar_t c) {
        return space_characters.find(c) == std::wstring::npos;
      }).value_or(ColumnNumber(0) + source.size()));
}

}  // namespace afc::language::lazy_string
