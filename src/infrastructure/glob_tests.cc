// Various unit tests for GlobMatcher.
//
// This can't be directly in //src/infrastructure:glob because the tests want
// to depend on it (so we'd have circular dependencies).

#include "src/infrastructure/glob.h"
#include "src/language/lazy_string/column_number.h"
#include "src/language/lazy_string/lazy_string.h"
#include "src/tests/tests.h"

using afc::language::lazy_string::ColumnNumber;
using afc::language::lazy_string::ColumnNumberDelta;
using afc::language::lazy_string::LazyString;
using afc::language::lazy_string::ToLazyString;

namespace afc::infrastructure {
namespace {
const bool tests_registration = tests::Register(
    L"GlobMatcher",
    {
        {.name = L"Simple",
         .callback =
             [] {
               auto results = GlobMatcher::New(LazyString{L"foo"})
                                  .Match(PathComponent::FromString(L"foo"));
               CHECK_EQ(results.pattern_prefix_size, ColumnNumberDelta{3});
               CHECK_EQ(results.component_prefix_size, ColumnNumberDelta{3});
             }},
        {.name = L"PatternFull",
         .callback =
             [] {
               auto results = GlobMatcher::New(LazyString{L"foo"})
                                  .Match(PathComponent::FromString(L"foobar"));
               CHECK_EQ(results.pattern_prefix_size, ColumnNumberDelta{3});
               CHECK_EQ(results.component_prefix_size, ColumnNumberDelta{3});
             }},
        {.name = L"ComponentFull",
         .callback =
             [] {
               auto results = GlobMatcher::New(LazyString{L"foobar"})
                                  .Match(PathComponent::FromString(L"fo"));
               CHECK_EQ(results.pattern_prefix_size, ColumnNumberDelta{2});
               CHECK_EQ(results.component_prefix_size, ColumnNumberDelta{2});
             }},
        {.name = L"SingleWildcard",
         .callback =
             [] {
               auto results = GlobMatcher::New(LazyString{L"*"})
                                  .Match(PathComponent::FromString(L"quux"));
               CHECK_EQ(results.pattern_prefix_size, ColumnNumberDelta{1});
               CHECK_EQ(results.component_prefix_size, ColumnNumberDelta{4});
             }},
        {.name = L"AdvancedWildcard",
         .callback =
             [] {
               auto results = GlobMatcher::New(LazyString{L"a*a"})
                                  .Match(PathComponent::FromString(L"aba"));
               CHECK_EQ(results.pattern_prefix_size, ColumnNumberDelta{3});
               CHECK_EQ(results.component_prefix_size, ColumnNumberDelta{3});
             }},
        {.name = L"AdvancedWildcardPrefix",
         .callback =
             [] {
               auto results = GlobMatcher::New(LazyString{L"a*a"})
                                  .Match(PathComponent::FromString(L"abcd"));
               CHECK_EQ(results.pattern_prefix_size, ColumnNumberDelta{2});
               CHECK_EQ(results.component_prefix_size, ColumnNumberDelta{4});
             }},
    });
}  // namespace
}  // namespace afc::infrastructure
