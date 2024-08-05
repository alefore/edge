#include "src/infrastructure/time_human.h"

#include "src/language/lazy_string/lazy_string.h"

using afc::language::lazy_string::LazyString;

namespace afc::infrastructure {
language::ValueOrError<std::wstring> HumanReadableTime(
    const struct timespec& time) {
  using language::Error;
  using language::Success;
  struct tm tm_value;
  if (localtime_r(&time.tv_sec, &tm_value) == nullptr)
    return Error{LazyString{L"localtime_r failed"}};
  char buffer[1024];
  size_t len = strftime(buffer, sizeof(buffer), "%Y-%m-%e %T %z", &tm_value);
  if (len == 0) return Error{LazyString{L"strftime failed"}};
  snprintf(buffer + len, sizeof(buffer) - len, ".%09ld", time.tv_nsec);
  return Success(language::FromByteString(std::string(buffer, strlen(buffer))));
}
}  // namespace afc::infrastructure
