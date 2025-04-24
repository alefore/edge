#ifndef __AFC_LANGUAGE_LAZY_STRING_FUNCTIONAL_H__
#define __AFC_LANGUAGE_LAZY_STRING_FUNCTIONAL_H__

#include <optional>
#include <unordered_set>

#include "src/language/hash.h"
#include "src/language/lazy_string/lazy_string.h"

namespace afc::language::lazy_string {
// Finds the first column in a string where `predicate` returns true.
//
// If no such column is found, returns an empty optional; otherwise, returns the
// first column found.
//
// `predicate` receives two argumens: the ColumnNumber and the character at that
// position.
template <typename StringType, typename Predicate>
std::optional<ColumnNumber> FindFirstColumnWithPredicate(
    const StringType& input, const Predicate& f, ColumnNumber start) {
  CHECK_LE(start.ToDelta(), input.size());
  for (ColumnNumberDelta delta = start.ToDelta(); delta < input.size(); ++delta)
    if (ColumnNumber column = ColumnNumber() + delta;
        f(column, input.get(column)))
      return column;
  return std::nullopt;
}

template <typename StringType, typename Predicate>
std::optional<ColumnNumber> FindFirstColumnWithPredicate(
    const StringType& input, const Predicate& f) {
  return FindFirstColumnWithPredicate(input, f, ColumnNumber{});
}

template <typename StringType, typename Predicate>
std::optional<ColumnNumber> FindLastColumnWithPredicate(const StringType& input,
                                                        const Predicate& f,
                                                        ColumnNumber end) {
  CHECK_LT(end.ToDelta(), input.size());
  for (ColumnNumberDelta delta; delta <= end.ToDelta(); ++delta)
    if (ColumnNumber column = end - delta; f(column, input.get(column)))
      return column;
  return std::nullopt;
}

template <typename StringType, typename Predicate>
std::optional<ColumnNumber> FindLastColumnWithPredicate(const StringType& input,
                                                        const Predicate& f) {
  if (input.empty()) return std::nullopt;
  return FindLastColumnWithPredicate(
      input, f, ColumnNumber{} + input.size() - ColumnNumberDelta{1});
}

template <typename StringType, typename Callback>
void ForEachColumn(const StringType& input, Callback callback) {
  FindFirstColumnWithPredicate(input, [&](ColumnNumber column, wchar_t c) {
    callback(column, c);
    return false;
  });
}

template <typename StringType>
std::optional<ColumnNumber> FindFirstOf(
    const StringType& input, const std::unordered_set<wchar_t>& chars) {
  return FindFirstOf(input, chars, ColumnNumber{});
}

template <typename StringType>
std::optional<ColumnNumber> FindFirstOf(
    const StringType& input, const std::unordered_set<wchar_t>& chars,
    ColumnNumber start) {
  return FindFirstColumnWithPredicate(
      input, [&chars](ColumnNumber, wchar_t c) { return chars.contains(c); },
      start);
}

template <typename StringType>
std::optional<ColumnNumber> FindFirstNotOf(
    const StringType& input, const std::unordered_set<wchar_t>& chars) {
  return FindFirstNotOf(input, chars, ColumnNumber{});
}

template <typename StringType>
std::optional<ColumnNumber> FindFirstNotOf(
    const StringType& input, const std::unordered_set<wchar_t>& chars,
    ColumnNumber start) {
  return FindFirstColumnWithPredicate(
      input, [&chars](ColumnNumber, wchar_t c) { return !chars.contains(c); },
      start);
}

// The search only includes characters at or before position `pos`.
template <typename StringType>
std::optional<ColumnNumber> FindLastOf(const StringType& input,
                                       const std::unordered_set<wchar_t>& chars,
                                       ColumnNumber pos) {
  return FindLastColumnWithPredicate(
      input, [&chars](ColumnNumber, wchar_t c) { return chars.contains(c); },
      pos);
}

template <typename StringType>
std::optional<ColumnNumber> FindLastOf(
    const StringType& input, const std::unordered_set<wchar_t>& chars) {
  if (input.empty()) return std::nullopt;
  return FindLastOf(input, chars,
                    ColumnNumber{} + input.size() - ColumnNumberDelta{1});
}

template <typename StringType>
std::optional<ColumnNumber> FindLastNotOf(
    const StringType& input, const std::unordered_set<wchar_t>& chars) {
  return FindLastColumnWithPredicate(
      input, [&chars](ColumnNumber, wchar_t c) { return !chars.contains(c); });
}

template <typename StringType, typename StringTypePrefix>
bool StartsWith(const StringType& input, const StringTypePrefix& prefix) {
  return ToLazyString(input.SubstringWithRangeChecks(
             ColumnNumber{}, prefix.size())) == ToLazyString(prefix);
}

std::vector<LazyString> SplitAt(LazyString input, wchar_t separator);
}  // namespace afc::language::lazy_string
namespace std {
template <>
struct hash<afc::language::lazy_string::LazyString> {
  std::size_t operator()(
      const afc::language::lazy_string::LazyString& input) const;
};
}  // namespace std

#endif  // __AFC_LANGUAGE_LAZY_STRING_FUNCTIONAL_H__
