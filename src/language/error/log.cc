#include "src/language/error/log.h"

#include "src/tests/tests.h"

namespace afc::language::error {
Log::InsertResult Log::Insert(language::Error error,
                              infrastructure::Duration duration) {
  infrastructure::Time now = infrastructure::Now();
  return entries_.lock([&](std::vector<ErrorAndExpiration>& entries) {
    std::erase_if(entries, [now](const ErrorAndExpiration& entry) {
      return entry.expiration < now;
    });

    InsertResult output = InsertResult::kInserted;
    for (const ErrorAndExpiration& entry : entries)
      if (entry.error == error) output = InsertResult::kAlreadyFound;
    entries.push_back(ErrorAndExpiration{
        .error = std::move(error),
        .expiration = infrastructure::AddSeconds(now, duration)});
    return output;
  });
}

namespace {
const bool tests_registration = tests::Register(
    L"error::Log",
    {{.name = L"Creation", .callback = [] { Log(); }},
     {.name = L"InsertIndependent",
      .callback =
          [] {
            Log log;
            CHECK(log.Insert(Error(L"Foo"), 1000) ==
                  Log::InsertResult::kInserted);
            CHECK(log.Insert(Error(L"Bar"), 1000) ==
                  Log::InsertResult::kInserted);
          }},
     {.name = L"InsertFinds",
      .callback =
          [] {
            Log log;
            CHECK(log.Insert(Error(L"Foo"), 1000) ==
                  Log::InsertResult::kInserted);
            CHECK(log.Insert(Error(L"Foo"), 1000) ==
                  Log::InsertResult::kAlreadyFound);
          }},
     {.name = L"InsertExpires", .callback = [] {
        Log log;
        CHECK(log.Insert(Error(L"Foo"), 1) == Log::InsertResult::kInserted);
        CHECK(log.Insert(Error(L"Foo"), 1) == Log::InsertResult::kAlreadyFound);
        sleep(2);
        CHECK(log.Insert(Error(L"Foo"), 1) == Log::InsertResult::kInserted);
      }}});
}

}  // namespace afc::language::error
