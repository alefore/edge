#include "src/language/lazy_string/functional.h"

#include "src/tests/tests.h"

namespace afc::language::lazy_string {
namespace {
const bool starts_with_tests_registration = tests::Register(
    L"LazyString.StartsWith",
    {
        {.name = L"AllEmpty",
         .callback = [] { CHECK(StartsWith(LazyString{}, LazyString{})); }},
        {.name = L"EmptyInput",
         .callback =
             [] { CHECK(!StartsWith(LazyString{}, LazyString{L"foo"})); }},
        {.name = L"EmptyPrefix",
         .callback =
             [] { CHECK(StartsWith(LazyString{L"foo"}, LazyString{})); }},
        {.name = L"HasPrefix",
         .callback =
             [] {
               CHECK(StartsWith(LazyString{L"foobar"}, LazyString{L"foob"}));
             }},
        {.name = L"DifferentPrefix",
         .callback =
             [] {
               CHECK(!StartsWith(LazyString{L"foobar"}, LazyString{L"foab"}));
             }},
    });
}  // namespace
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
