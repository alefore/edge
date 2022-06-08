#ifndef __AFC_LANGUAGE_LAZY_STRING_FUNCTIONAL_H__
#define __AFC_LANGUAGE_LAZY_STRING_FUNCTIONAL_H__

#include <optional>

#include "src/language/hash.h"
#include "src/language/lazy_string/lazy_string.h"

namespace afc::editor {
// Finds the first column in a string where `predicate` returns true.
//
// If no such column is found, returns an empty optional; otherwise, returns the
// first column found.
//
// `predicate` receives two argumens: the ColumnNumber and the character at that
// position.
template <typename Predicate>
std::optional<ColumnNumber> FindFirstColumnWithPredicate(
    const LazyString& input, const Predicate& f) {
  for (ColumnNumber column; column < ColumnNumber(0) + input.size(); ++column) {
    if (f(column, input.get(column))) {
      return column;
    }
  }
  return std::nullopt;
}

template <typename Callback>
void ForEachColumn(const LazyString& input, Callback callback) {
  FindFirstColumnWithPredicate(input, [&](ColumnNumber column, wchar_t c) {
    callback(column, c);
    return false;
  });
}
}  // namespace afc::editor
namespace std {
template <>
struct hash<afc::editor::LazyString> {
  std::size_t operator()(const afc::editor::LazyString& input) const;
};
}  // namespace std

#endif  // __AFC_LANGUAGE_LAZY_STRING_FUNCTIONAL_H__
