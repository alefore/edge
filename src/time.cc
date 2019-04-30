#include "src/time.h"

#include <glog/logging.h>

#include <memory>

namespace afc {
namespace editor {

double MillisecondsBetween(const struct timespec& begin,
                           const struct timespec& end) {
  return (end.tv_sec - begin.tv_sec) * 1000 +
         (end.tv_nsec - begin.tv_nsec) / 1000000;
}

double GetElapsedMillisecondsSince(const struct timespec& spec) {
  struct timespec copy = spec;
  return GetElapsedMillisecondsAndUpdate(&copy);
}

double GetElapsedMillisecondsAndUpdate(struct timespec* spec) {
  struct timespec now;
  if (clock_gettime(0, &now) == -1) {
    return 0;
  }
  double output = MillisecondsBetween(*spec, now);
  *spec = now;
  return output;
}

std::optional<double> UpdateIfMillisecondsHavePassed(
    struct timespec* spec, double required_milliseconds) {
  struct timespec now;
  if (clock_gettime(0, &now) == -1) {
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
