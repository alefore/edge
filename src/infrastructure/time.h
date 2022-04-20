#ifndef __AFC_EDITOR_TIME_H__
#define __AFC_EDITOR_TIME_H__

#include <optional>
#include <string>

#include "src/language/value_or_error.h"

namespace afc::infrastructure {
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

struct timespec AddSeconds(struct timespec time, double seconds_duration);

language::ValueOrError<std::wstring> HumanReadableTime(
    const struct timespec& time);
}  // namespace afc::infrastructure

bool operator==(const struct timespec& a, const struct timespec& b);
bool operator!=(const struct timespec& a, const struct timespec& b);
bool operator<(const struct timespec& a, const struct timespec& b);
bool operator>(const struct timespec& a, const struct timespec& b);
bool operator<=(const struct timespec& a, const struct timespec& b);
bool operator>=(const struct timespec& a, const struct timespec& b);

#endif  // __AFC_EDITOR_TIME_H__
