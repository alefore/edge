#include "src/language/errors/log.h"

namespace afc::language::error {
Log::InsertResult Log::Insert(language::Error error,
                              infrastructure::Duration duration) {
  infrastructure::Time now = infrastructure::Now();
  return entries_.lock([&](std::vector<ErrorAndExpiration> entries) {
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
}  // namespace afc::language::error
