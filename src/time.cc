#include "src/time.h"

#include <glog/logging.h>

#include <memory>

namespace afc {
namespace editor {

double SecondsBetween(const struct timespec& begin,
                      const struct timespec& end) {
  return (end.tv_sec - begin.tv_sec) +
         static_cast<double>(end.tv_nsec - begin.tv_nsec) * 1e-9;
}

double MillisecondsBetween(const struct timespec& begin,
                           const struct timespec& end) {
  return SecondsBetween(begin, end) / 1000.0;
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
  struct timespec now;
  if (clock_gettime(0, &now) == -1) {
    VLOG(5) << "clock_gettime failed.";
    return 0;
  }
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

}  // namespace editor
}  // namespace afc
