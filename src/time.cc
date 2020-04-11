#include "src/time.h"

#include <glog/logging.h>

#include <cmath>
#include <memory>

#include "src/wstring.h"

namespace afc {
namespace editor {

struct timespec Now() {
  struct timespec output;
  CHECK_NE(clock_gettime(0, &output), -1);
  return output;
}

double SecondsBetween(const struct timespec& begin,
                      const struct timespec& end) {
  return (end.tv_sec - begin.tv_sec) +
         static_cast<double>(end.tv_nsec - begin.tv_nsec) * 1e-9;
}

double MillisecondsBetween(const struct timespec& begin,
                           const struct timespec& end) {
  return SecondsBetween(begin, end) * 1000.0;
}

double GetElapsedSecondsSince(const struct timespec& spec) {
  struct timespec copy = spec;
  return GetElapsedSecondsAndUpdate(&copy);
}

double GetElapsedMillisecondsSince(const struct timespec& spec) {
  return GetElapsedSecondsSince(spec) * 1000;
}

double GetElapsedMillisecondsAndUpdate(struct timespec* spec) {
  return GetElapsedSecondsAndUpdate(spec) * 1000;
}

double GetElapsedSecondsAndUpdate(struct timespec* spec) {
  struct timespec now = Now();
  double output = SecondsBetween(*spec, now);
  VLOG(6) << "Elapsed seconds: " << output;
  *spec = now;
  return output;
}

std::optional<double> UpdateIfMillisecondsHavePassed(
    struct timespec* spec, double required_milliseconds) {
  struct timespec now;
  if (clock_gettime(0, &now) == -1) {
    VLOG(5) << "clock_gettime failed.";
    return std::nullopt;
  }
  double elapsed = MillisecondsBetween(*spec, now);
  if (elapsed < required_milliseconds) {
    return std::nullopt;
  }
  *spec = now;
  return elapsed;
}

struct timespec AddSeconds(struct timespec time, double seconds_duration) {
  double int_part;
  double dec_part = modf(seconds_duration, &int_part);
  time.tv_nsec += static_cast<long>(dec_part * 1e9);
  if (time.tv_nsec > 1e9) {
    time.tv_sec += time.tv_nsec / 1000000000;
    time.tv_nsec = time.tv_nsec % 1000000000;
  }
  time.tv_sec += static_cast<time_t>(int_part);
  return time;
}

ValueOrError<std::wstring> HumanReadableTime(const struct timespec& time) {
  struct tm tm_value;
  if (localtime_r(&time.tv_sec, &tm_value) == nullptr)
    return Error(L"localtime_r failed");
  char buffer[1024];
  size_t len = strftime(buffer, sizeof(buffer), "%Y-%m-%e %T %z", &tm_value);
  if (len == 0) return Error(L"strftime failed");
  snprintf(buffer + len, sizeof(buffer) - len, ".%09ld", time.tv_nsec);
  return Success(FromByteString(std::string(buffer, strlen(buffer))));
}

}  // namespace editor
}  // namespace afc

bool operator<(const struct timespec& a, const struct timespec& b) {
  return a.tv_sec < b.tv_sec || (a.tv_sec == b.tv_sec && a.tv_nsec < b.tv_nsec);
}
