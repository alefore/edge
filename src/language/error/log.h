#ifndef __AFC_EDITOR_LANGUAGE_ERROR_LOG_H__
#define __AFC_EDITOR_LANGUAGE_ERROR_LOG_H__

#include "src/concurrent/protected.h"
#include "src/infrastructure/time.h"
#include "src/language/error/value_or_error.h"

namespace afc::language::error {

// This class is thread-safe.
class Log {
 public:
  enum class InsertResult { kInserted, kAlreadyFound };
  InsertResult Insert(language::Error error, infrastructure::Duration duration);

 private:
  struct ErrorAndExpiration {
    language::Error error;
    infrastructure::Time expiration;
  };

  concurrent::Protected<std::vector<ErrorAndExpiration>> entries_;
};
}  // namespace afc::language::error

#endif  // __AFC_EDITOR_LANGUAGE_ERROR_LOG_H__
