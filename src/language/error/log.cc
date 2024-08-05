#include "src/language/error/log.h"

#include <ranges>

#include "src/language/container.h"
#include "src/tests/tests.h"

using afc::language::EraseIf;
using afc::language::lazy_string::LazyString;

namespace afc::language::error {
Log::InsertResult Log::Insert(language::Error error,
                              infrastructure::Duration duration) {
  infrastructure::Time now = infrastructure::Now();
  return entries_.lock([&](std::vector<ErrorAndExpiration>& entries) {
    EraseIf(entries, [now](const ErrorAndExpiration& entry) {
      return entry.expiration < now;
    });
    InsertResult output =
        std::ranges::contains(
            entries | std::views::transform(&ErrorAndExpiration::error), error)
            ? InsertResult::kAlreadyFound
            : InsertResult::kInserted;
    entries.push_back(ErrorAndExpiration{
        .error = std::move(error),
        .expiration = infrastructure::AddSeconds(now, duration)});
    return output;
  });
}

namespace {
const bool tests_registration = tests::Register(
    L"error::Log", {{.name = L"Creation", .callback = [] { Log(); }},
                    {.name = L"InsertIndependent",
                     .callback =
                         [] {
                           Log log;
                           CHECK(log.Insert(Error{LazyString{L"Foo"}}, 1000) ==
                                 Log::InsertResult::kInserted);
                           CHECK(log.Insert(Error{LazyString{L"Bar"}}, 1000) ==
                                 Log::InsertResult::kInserted);
                         }},
                    {.name = L"InsertFinds",
                     .callback =
                         [] {
                           Log log;
                           CHECK(log.Insert(Error{LazyString{L"Foo"}}, 1000) ==
                                 Log::InsertResult::kInserted);
                           CHECK(log.Insert(Error{LazyString{L"Foo"}}, 1000) ==
                                 Log::InsertResult::kAlreadyFound);
                         }},
                    {.name = L"InsertExpires", .callback = [] {
                       Log log;
                       CHECK(log.Insert(Error{LazyString{L"Foo"}}, 1) ==
                             Log::InsertResult::kInserted);
                       CHECK(log.Insert(Error{LazyString{L"Foo"}}, 1) ==
                             Log::InsertResult::kAlreadyFound);
                       sleep(2);
                       CHECK(log.Insert(Error{LazyString{L"Foo"}}, 1) ==
                             Log::InsertResult::kInserted);
                     }}});
}  // namespace

}  // namespace afc::language::error
