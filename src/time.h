#ifndef __AFC_EDITOR_TIME_H__
#define __AFC_EDITOR_TIME_H__

#include <optional>
#include <string>

#include "src/value_or_error.h"

namespace afc {
namespace editor {
struct timespec Now();

// TODO: Replace all this with Abseil.
double SecondsBetween(const struct timespec& begin, const struct timespec& end);
double MillisecondsBetween(const struct timespec& begin,
                           const struct timespec& end);
double GetElapsedSecondsSince(const struct timespec& spec);
double GetElapsedMillisecondsSince(const struct timespec& spec);

double GetElapsedMillisecondsAndUpdate(struct timespec* spec);
double GetElapsedSecondsAndUpdate(struct timespec* spec);

// If required_milliseconds have passed since the last call, updates `spec` to
// now and returns the elapsed time. Otherwise, returns nullopt.
std::optional<double> UpdateIfMillisecondsHavePassed(
    struct timespec* spec, double required_milliseconds);

ValueOrError<std::wstring> HumanReadableTime(const struct timespec& time);
}  // namespace editor
}  // namespace afc

bool operator<(const struct timespec& a, const struct timespec& b);

#endif  // __AFC_EDITOR_TIME_H__
