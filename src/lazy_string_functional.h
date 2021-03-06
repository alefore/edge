#ifndef __AFC_EDITOR_LAZY_STRING_FUNCTIONAL_H__
#define __AFC_EDITOR_LAZY_STRING_FUNCTIONAL_H__

#include <memory>
#include <optional>
#include <string>

#include "line_column.h"

namespace afc {
namespace editor {

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

}  // namespace editor
}  // namespace afc

#endif  // __AFC_EDITOR_LAZY_STRING_FUNCTIONAL_H__
