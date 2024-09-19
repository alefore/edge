#include "src/language/lazy_string/trim.h"

#include <glog/logging.h>

#include "src/infrastructure/tracker.h"
#include "src/language/lazy_string/functional.h"
#include "src/tests/tests.h"

namespace afc::language::lazy_string {

LazyString Trim(LazyString in) { return Trim(in, {L' '}); }
SingleLine Trim(SingleLine in) { return Trim(in, {L' '}); }

namespace {
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
