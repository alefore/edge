#include "src/language/lazy_string/trim.h"

#include <glog/logging.h>

#include "src/lazy_string_functional.h"
#include "src/substring.h"

namespace afc::editor {
using language::NonNull;

NonNull<std::shared_ptr<LazyString>> StringTrimLeft(
    NonNull<std::shared_ptr<LazyString>> source,
    std::wstring space_characters) {
  return Substring(
      source, FindFirstColumnWithPredicate(source.value(), [&](ColumnNumber,
                                                               wchar_t c) {
                return space_characters.find(c) == std::wstring::npos;
              }).value_or(ColumnNumber(0) + source->size()));
}

}  // namespace afc::editor
