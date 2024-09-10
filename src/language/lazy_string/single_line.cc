#include "src/language/lazy_string/single_line.h"

#include "src/language/error/value_or_error.h"
#include "src/language/lazy_string/functional.h"

namespace afc::language::lazy_string {
/* static */ language::PossibleError SingleLineValidator::Validate(
    const LazyString& input) {
  if (FindFirstOf(input, {L'\n'}).has_value())
    return Error{LazyString{L"SingleLine contained newline character."}};
  return EmptyValue{};
}

SingleLine SingleLine::Substring(ColumnNumber a) const {
  return SingleLine{read().Substring(a)};
}

SingleLine SingleLine::Substring(ColumnNumber a, ColumnNumberDelta b) const {
  return SingleLine{read().Substring(a, b)};
}

SingleLine SingleLine::Append(SingleLine other) const {
  return SingleLine{read().Append(other.read())};
}

SingleLine operator+(const SingleLine& a, const SingleLine& b) {
  return SingleLine{a.read() + b.read()};
}
}  // namespace afc::language::lazy_string
