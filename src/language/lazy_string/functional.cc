#include "src/language/lazy_string/functional.h"

namespace afc::language::lazy_string {
std::optional<ColumnNumber> FindFirstOf(const LazyString& input,
                                        std::unordered_set<wchar_t> chars) {
  return FindFirstColumnWithPredicate(
      input, [chars = std::move(chars)](ColumnNumber, wchar_t c) {
        return chars.contains(c);
      });
}

std::optional<ColumnNumber> FindFirstNotOf(const LazyString& input,
                                           std::unordered_set<wchar_t> chars) {
  return FindFirstColumnWithPredicate(
      input, [chars = std::move(chars)](ColumnNumber, wchar_t c) {
        return !chars.contains(c);
      });
}

std::optional<ColumnNumber> FindLastOf(const LazyString& input,
                                       std::unordered_set<wchar_t> chars) {
  return FindLastColumnWithPredicate(
      input, [chars = std::move(chars)](ColumnNumber, wchar_t c) {
        return chars.contains(c);
      });
}

std::optional<ColumnNumber> FindLastNotOf(const LazyString& input,
                                          std::unordered_set<wchar_t> chars) {
  return FindLastColumnWithPredicate(
      input, [chars = std::move(chars)](ColumnNumber, wchar_t c) {
        return !chars.contains(c);
      });
}
}  // namespace afc::language::lazy_string

namespace std {
using afc::language::hash_combine;
using afc::language::MakeHashableIteratorRange;

std::size_t hash<afc::language::lazy_string::LazyString>::operator()(
    const afc::language::lazy_string::LazyString& input) const {
  size_t value = 302948;
  ForEachColumn(input, [&](afc::language::lazy_string::ColumnNumber,
                           wchar_t c) { value = hash_combine(value, c); });
  return value;
}
}  // namespace std
