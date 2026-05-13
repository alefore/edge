#include "src/language/lazy_string/functional.h"
#include "src/tests/factory.h"
#include "src/tests/tests.h"

namespace afc::language::lazy_string {
namespace {

TEST_GROUP(LazyString_StartsWith,
           [](std::pair<LazyString, LazyString> input) {
             return StartsWith(input.first, input.second);
           })
    .Add(L"AllEmpty", {L"", L""}, true)
    .Add(L"EmptyInput", {L"", L"foo"}, false)
    .Add(L"EmptyPrefix", {L"foo", L""}, true)
    .Add(L"HasPrefix", {L"foobar", L"foob"}, true)
    .Add(L"DifferentPrefix", {L"foobar", L"foab"}, false);

TEST_GROUP(LazyString_EndsWith,
           [](std::pair<LazyString, LazyString> input) {
             return EndsWith(input.first, input.second);
           })
    .Add(L"AllEmpty", {L"", L""}, true)
    .Add(L"EmptyInput", {L"", L"foo"}, false)
    .Add(L"EmptySuffix", {L"foo", L""}, true)
    .Add(L"HasSuffix", {L"foobar", L"obar"}, true)
    .Add(L"DifferentSuffix", {L"foobar", L"oabar"}, false);

TEST_GROUP(LazyString_FindFirstOf,
           ([](std::tuple<LazyString, std::unordered_set<wchar_t>, ColumnNumber>
                   input) {
             auto [str, chars, col] = input;
             return FindFirstOf(str, chars, col);
           }))
    .Add(L"Empty", {L"", {L':'}, ColumnNumber{0}}, std::nullopt)
    .Add(L"None", {L"foobar", {L':'}, ColumnNumber{0}}, std::nullopt)
    .Add(L"AtResult",
         {L"/home/alejo/edge-clang/edge/src/futures::ValueOrError",
          {L':'},
          ColumnNumber{40}},
         ColumnNumber{40})
    .Add(L"PastLast",
         {L"/home/alejo/edge-clang/edge/src/futures::ValueOrError",
          {L':'},
          ColumnNumber{41}},
         std::nullopt)
    .Add(L"InBetween", {L"/foo/bar/hey/there", {L'/'}, ColumnNumber{2}},
         ColumnNumber{4});

}  // namespace
}  // namespace afc::language::lazy_string
