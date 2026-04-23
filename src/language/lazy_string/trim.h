#ifndef __AFC_LANGUAGE_LAZY_STRING_TRIM_H__
#define __AFC_LANGUAGE_LAZY_STRING_TRIM_H__

#include <string>
#include <unordered_set>

#include "src/infrastructure/tracker.h"
#include "src/language/lazy_string/lazy_string.h"
#include "src/language/lazy_string/single_line.h"
#include "src/language/safe_types.h"

namespace afc::language::lazy_string {
// Returns a copy with all left trim_characters removed.
template <typename StringType>
StringType TrimLeft(StringType source,
                    const std::unordered_set<wchar_t>& trim_characters) {
  TRACK_OPERATION(LazyString_StringTrimLeft);
  return source.Substring(FindFirstNotOf(source, trim_characters)
                              .value_or(ColumnNumber(0) + source.size()));
}

// Returns a copy with all left trim_characters removed.
template <typename StringType>
StringType TrimRight(StringType source,
                     const std::unordered_set<wchar_t>& trim_characters) {
  TRACK_OPERATION(LazyString_StringTrimRight);
  if (std::optional<ColumnNumber> end = FindLastNotOf(source, trim_characters);
      end.has_value())
    // Why add ColumnNumberDelta{1}? To include the last-not-of character.
    return source.Substring(ColumnNumber{},
                            end->ToDelta() + ColumnNumberDelta{1});
  return source;
}

// Returns a copy with all left and right trim_characters removed.
//
// StringType is expected to be either LazyString or SingleLine.
template <typename StringType>
StringType Trim(StringType in,
                const std::unordered_set<wchar_t>& trim_characters) {
  if (std::optional<ColumnNumber> begin = FindFirstNotOf(in, trim_characters);
      begin.has_value()) {
    if (std::optional<ColumnNumber> end = FindLastNotOf(in, trim_characters);
        end.has_value())
      return in.Substring(*begin, *end - *begin + ColumnNumberDelta{1});
  }
  return StringType{};
}
}  // namespace afc::language::lazy_string

#endif  // __AFC_LANGUAGE_LAZY_STRING_TRIM_H__
