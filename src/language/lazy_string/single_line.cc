#include "src/language/lazy_string/single_line.h"

#include "src/language/error/value_or_error.h"
#include "src/language/lazy_string/functional.h"
#include "src/language/lazy_string/lowercase.h"

namespace afc::language::lazy_string {
/* static */ language::PossibleError SingleLineValidator::Validate(
    const LazyString& input) {
  if (FindFirstOf(input, {L'\n'}).has_value())
    return Error{LazyString{L"SingleLine contained newline character."}};
  return EmptyValue{};
}

wchar_t SingleLine::get(ColumnNumber a) const { return read().get(a); }

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

std::optional<ColumnNumber> FindLastNotOf(const SingleLine& input,
                                          std::unordered_set<wchar_t> chars) {
  return FindLastNotOf(input.read(), std::move(chars));
}

SingleLine LowerCase(SingleLine input) {
  return SingleLine{LowerCase(input.read())};
}
}  // namespace afc::language::lazy_string
