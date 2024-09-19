#ifndef __AFC_LANGUAGE_LAZY_STRING_TRIM_H__
#define __AFC_LANGUAGE_LAZY_STRING_TRIM_H__

#include <string>
#include <unordered_set>

#include "src/infrastructure/tracker.h"
#include "src/language/lazy_string/lazy_string.h"
#include "src/language/lazy_string/single_line.h"
#include "src/language/safe_types.h"

namespace afc::language::lazy_string {
// Returns a copy with all left space characters removed.
template <typename StringType>
StringType TrimLeft(StringType source, std::wstring space_characters) {
  TRACK_OPERATION(LazyString_StringTrimLeft);
  return source.Substring(
      FindFirstColumnWithPredicate(source, [&](ColumnNumber, wchar_t c) {
        return space_characters.find(c) == std::wstring::npos;
      }).value_or(ColumnNumber(0) + source.size()));
}

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
