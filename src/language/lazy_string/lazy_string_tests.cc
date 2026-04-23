#include "src/language/lazy_string/lazy_string.h"
#include "src/tests/tests.h"

namespace afc::language::lazy_string {
namespace {
const bool iterator_tests_registration = tests::Register(
    L"LazyStringIterator",
    {{.name = L"EndComparisonOk",
      .callback =
          [] { CHECK(LazyString{L""}.end() != LazyString{L""}.end()); }},
     {.name = L"EmptyBeginComparisonOk",
      .callback =
          [] { CHECK(LazyString{L""}.begin() != LazyString{L""}.begin()); }},
     {.name = L"ComparisonEqual",
      .callback =
          [] {
            LazyString input{L"alejandro"};
            CHECK(input.begin() == input.begin());
          }},
     {.name = L"ComparisonDifferent",
      .callback =
          [] {
            LazyString input{L"alejandro"};
            CHECK(++input.begin() != input.begin());
          }},
     {.name = L"ComparisonDifferentContainersCrashes",
      .callback =
          [] {
            tests::ForkAndWaitForFailure([] {
              // Cast is to silence a compiler warning: unused result.
              (void)(LazyString{L"a"}.begin() == LazyString{L"a"}.begin());
            });
          }},
     {.name = L"EventuallyReachesEnd", .callback = [] {
        LazyString input{L"foo"};
        LazyStringIterator it = input.begin();
        ++it;
        ++it;
        ++it;
        CHECK(it == input.end());
      }}});

}  // namespace
}  // namespace afc::language::lazy_string
