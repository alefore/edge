#include "src/infrastructure/time_human.h"

#include "src/language/lazy_string/lazy_string.h"
#include "src/language/lazy_string/single_line.h"

using afc::language::Error;
using afc::language::Success;
using afc::language::lazy_string::LazyString;
using afc::language::lazy_string::NonEmptySingleLine;
using afc::language::lazy_string::SingleLine;

namespace afc::infrastructure {
namespace {
language::ValueOrError<NonEmptySingleLine> strftime(const struct timespec& time,
                                                    const char* spec) {
  struct tm tm_value;
  if (localtime_r(&time.tv_sec, &tm_value) == nullptr)
    return Error{LazyString{L"localtime_r failed"}};
  char buffer[1024];
  size_t len = strftime(buffer, sizeof(buffer), spec, &tm_value);
  if (len == 0) return Error{LazyString{L"strftime failed"}};
  snprintf(buffer + len, sizeof(buffer) - len, ".%09ld", time.tv_nsec);
  return NonEmptySingleLine::New(SingleLine::New(LazyString{
      language::FromByteString(std::string(buffer, strlen(buffer)))}));
}
}  // namespace

language::ValueOrError<NonEmptySingleLine> HumanReadableTime(const Time& time) {
  return strftime(time, "%Y-%m-%d %T %z");
}

language::ValueOrError<NonEmptySingleLine> HumanReadableDate(const Time& time) {
  return strftime(time, "%Y-%m-%d");
}
}  // namespace afc::infrastructure
