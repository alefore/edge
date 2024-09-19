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

/* static */ language::PossibleError NonEmptySingleLineValidator::Validate(
    const SingleLine& input) {
  if (input.empty()) return Error{LazyString{L"NonEmptySingleLine was empty."}};
  return EmptyValue{};
}

wchar_t SingleLine::get(ColumnNumber a) const { return read().get(a); }

SingleLine SingleLine::Substring(ColumnNumber a) const {
  return SingleLine{read().Substring(a)};
}

SingleLine SingleLine::Substring(ColumnNumber a, ColumnNumberDelta b) const {
  return SingleLine{read().Substring(a, b)};
}

SingleLine SingleLine::SubstringWithRangeChecks(ColumnNumber a,
                                                ColumnNumberDelta b) const {
  return SingleLine{read().SubstringWithRangeChecks(a, b)};
}

SingleLine SingleLine::Append(SingleLine other) const {
  return SingleLine{read().Append(other.read())};
}

wchar_t NonEmptySingleLine::get(ColumnNumber a) const { return read().get(a); }

SingleLine NonEmptySingleLine::Substring(ColumnNumber start) const {
  return read().Substring(start);
}

SingleLine NonEmptySingleLine::Substring(ColumnNumber start,
                                         ColumnNumberDelta len) const {
  return read().Substring(start, len);
}

SingleLine NonEmptySingleLine::SubstringWithRangeChecks(
    ColumnNumber start, ColumnNumberDelta len) const {
  return read().SubstringWithRangeChecks(start, len);
}

LazyString operator+(const LazyString& a, const SingleLine& b) {
  return a + b.read();
}

LazyString operator+(const SingleLine& a, const LazyString& b) {
  return a.read() + b;
}

SingleLine operator+(const SingleLine& a, const SingleLine& b) {
  return SingleLine{a.read() + b.read()};
}

NonEmptySingleLine operator+(const SingleLine& a, const NonEmptySingleLine& b) {
  return NonEmptySingleLine{a + b.read()};
}

NonEmptySingleLine operator+(const NonEmptySingleLine& a, const SingleLine& b) {
  return NonEmptySingleLine{a.read() + b};
}

NonEmptySingleLine operator+(const NonEmptySingleLine& a,
                             const NonEmptySingleLine& b) {
  return NonEmptySingleLine{a.read() + b.read()};
}

std::optional<ColumnNumber> FindLastNotOf(const SingleLine& input,
                                          std::unordered_set<wchar_t> chars) {
  return FindLastNotOf(input.read(), std::move(chars));
}
}  // namespace afc::language::lazy_string
