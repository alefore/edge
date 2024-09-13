#ifndef __AFC_LANGUAGE_LAZY_STRING_SINGLE_LINE_H__
#define __AFC_LANGUAGE_LAZY_STRING_SINGLE_LINE_H__

#include <unordered_set>

#include "src/language/error/value_or_error.h"
#include "src/language/ghost_type_class.h"
#include "src/language/lazy_string/functional.h"
#include "src/language/lazy_string/lazy_string.h"

namespace afc::language::lazy_string {
struct SingleLineValidator {
  static language::PossibleError Validate(const LazyString& input);
};

class SingleLine
    : public GhostType<SingleLine, LazyString, SingleLineValidator> {
 public:
  wchar_t get(ColumnNumber) const;
  SingleLine Substring(ColumnNumber) const;
  SingleLine Substring(ColumnNumber, ColumnNumberDelta) const;
  SingleLine Append(SingleLine) const;
};

SingleLine operator+(const SingleLine& a, const SingleLine& b);

template <typename Predicate>
std::optional<ColumnNumber> FindFirstColumnWithPredicate(
    SingleLine line, const Predicate& predicate) {
  return FindFirstColumnWithPredicate(line.read(), predicate);
}

std::optional<ColumnNumber> FindLastNotOf(const SingleLine& input,
                                          std::unordered_set<wchar_t> chars);

template <typename Callback>
void ForEachColumn(const SingleLine& input, Callback&& callback) {
  ForEachColumn(input.read(), std::forward<Callback>(callback));
}

SingleLine LowerCase(SingleLine input);
}  // namespace afc::language::lazy_string

#endif
