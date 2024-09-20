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

namespace internal {
constexpr bool ContainsNewline(const wchar_t* input) {
  return std::wstring_view(input).find('\n') != std::wstring_view::npos;
}
}  // namespace internal

class SingleLine
    : public GhostType<SingleLine, LazyString, SingleLineValidator> {
 public:
  using GhostType::GhostType;

  template <const wchar_t* const input>
  static constexpr SingleLine FromConstant() {
    static_assert(!internal::ContainsNewline(input),
                  "String can't contain newline characters.");
    return SingleLine{LazyString{std::wstring{input}}};
  }

  template <wchar_t c = L' '>
  static constexpr SingleLine Padding(ColumnNumberDelta len) {
    static_assert(c != L'\n' && c != L'\r', "Character can't be newline.");
    return SingleLine{LazyString{len, c}};
  }

  template <wchar_t c>
  static constexpr SingleLine Char() {
    return Padding<c>(ColumnNumberDelta{1});
  }

  wchar_t get(ColumnNumber) const;
  SingleLine Substring(ColumnNumber) const;
  SingleLine Substring(ColumnNumber, ColumnNumberDelta) const;
  SingleLine SubstringWithRangeChecks(ColumnNumber, ColumnNumberDelta) const;
  SingleLine Append(SingleLine) const;
};

#define SINGLE_LINE_CONSTANT(x)                  \
  std::invoke([] {                               \
    static constexpr wchar_t kMessage[] = x;     \
    return SingleLine::FromConstant<kMessage>(); \
  })

struct NonEmptySingleLineValidator {
  static language::PossibleError Validate(const SingleLine& input);
};

class NonEmptySingleLine : public GhostType<NonEmptySingleLine, SingleLine,
                                            NonEmptySingleLineValidator> {
 public:
  using GhostType::GhostType;

  NonEmptySingleLine(int);
  NonEmptySingleLine(size_t);

  wchar_t get(ColumnNumber) const;
  SingleLine Substring(ColumnNumber) const;
  SingleLine Substring(ColumnNumber, ColumnNumberDelta) const;
  SingleLine SubstringWithRangeChecks(ColumnNumber, ColumnNumberDelta) const;
};

LazyString operator+(const LazyString& a, const SingleLine& b);
LazyString operator+(const SingleLine& a, const LazyString& b);
SingleLine operator+(const SingleLine& a, const SingleLine& b);
NonEmptySingleLine operator+(const SingleLine& a, const NonEmptySingleLine& b);
NonEmptySingleLine operator+(const NonEmptySingleLine& a, const SingleLine& b);
NonEmptySingleLine operator+(const NonEmptySingleLine& a,
                             const NonEmptySingleLine& b);

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

template <typename StringType>
auto Parenthesize(StringType input) {
  return SingleLine::Char<L'('>() + input + SingleLine::Char<L')'>();
}
}  // namespace afc::language::lazy_string

#endif
