#include "src/language/lazy_string/trim.h"

#include <glog/logging.h>

#include "src/infrastructure/tracker.h"
#include "src/language/lazy_string/functional.h"
#include "src/tests/tests.h"

namespace afc::language::lazy_string {
using infrastructure::Tracker;

LazyString TrimLeft(LazyString source, std::wstring space_characters) {
  static Tracker tracker(L"LazyString::StringTrimLeft");
  auto call = tracker.Call();
  return source.Substring(
      FindFirstColumnWithPredicate(source, [&](ColumnNumber, wchar_t c) {
        return space_characters.find(c) == std::wstring::npos;
      }).value_or(ColumnNumber(0) + source.size()));
}

LazyString Trim(LazyString in, std::unordered_set<wchar_t> space_characters) {
  if (std::optional<ColumnNumber> begin = FindFirstNotOf(in, space_characters);
      begin.has_value()) {
    if (std::optional<ColumnNumber> end = FindLastNotOf(in, space_characters);
        end.has_value())
      return in.Substring(*begin, *end - *begin + ColumnNumberDelta{1});
  }
  return LazyString{};
}

namespace {
LazyString Trim(LazyString in) { return Trim(in, {L' '}); }

const bool trim_whitespace_tests_registration = tests::Register(
    L"Trim",
    {
        {.name = L"Empty",
         .callback = [] { CHECK_EQ(Trim(LazyString{}), LazyString{}); }},
        {.name = L"OnlySpaces",
         .callback =
             [] { CHECK_EQ(Trim(LazyString{L"     "}), LazyString{}); }},
        {.name = L"NoTrim",
         .callback =
             [] {
               CHECK_EQ(Trim(LazyString{L"foo bar"}), LazyString{L"foo bar"});
             }},
        {.name = L"Prefix",
         .callback =
             [] {
               CHECK_EQ(Trim(LazyString{L"   foo bar"}),
                        LazyString{L"foo bar"});
             }},
        {.name = L"Suffix",
         .callback =
             [] {
               CHECK_EQ(Trim(LazyString{L"foo bar    "}),
                        LazyString{L"foo bar"});
             }},
        {.name = L"Both",
         .callback =
             [] {
               CHECK_EQ(Trim(LazyString{L" foo bar quux "}),
                        LazyString{L"foo bar quux"});
             }},
    });
}  // namespace
}  // namespace afc::language::lazy_string
