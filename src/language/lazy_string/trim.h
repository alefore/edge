#ifndef __AFC_LANGUAGE_LAZY_STRING_TRIM_H__
#define __AFC_LANGUAGE_LAZY_STRING_TRIM_H__

#include <string>
#include <unordered_set>

#include "src/language/lazy_string/lazy_string.h"
#include "src/language/lazy_string/single_line.h"
#include "src/language/safe_types.h"

namespace afc::language::lazy_string {
// Returns a copy with all left space characters removed.
LazyString TrimLeft(LazyString a, std::wstring space_characters);

// StringType is expected to be either LazyString or SingleLine.
template <typename StringType>
StringType Trim(StringType in, std::unordered_set<wchar_t> space_characters) {
  if (std::optional<ColumnNumber> begin = FindFirstNotOf(in, space_characters);
      begin.has_value()) {
    if (std::optional<ColumnNumber> end = FindLastNotOf(in, space_characters);
        end.has_value())
      return in.Substring(*begin, *end - *begin + ColumnNumberDelta{1});
  }
  return StringType{};
}
}  // namespace afc::language::lazy_string

#endif  // __AFC_LANGUAGE_LAZY_STRING_TRIM_H__
