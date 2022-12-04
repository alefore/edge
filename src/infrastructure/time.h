#ifndef __AFC_EDITOR_TIME_H__
#define __AFC_EDITOR_TIME_H__

#include <optional>
#include <string>

#include "src/language/value_or_error.h"

namespace afc::infrastructure {
using Time = struct timespec;

Time Now();

// TODO: Replace all this with Abseil.
double SecondsBetween(const Time& begin, const Time& end);
double MillisecondsBetween(const Time& begin, const Time& end);
double GetElapsedSecondsSince(const Time& spec);
double GetElapsedMillisecondsSince(const Time& spec);

double GetElapsedMillisecondsAndUpdate(Time* spec);
double GetElapsedSecondsAndUpdate(Time* spec);

// If required_milliseconds have passed since the last call, updates `spec` to
// now and returns the elapsed time. Otherwise, returns nullopt.
std::optional<double> UpdateIfMillisecondsHavePassed(
    Time* spec, double required_milliseconds);

Time AddSeconds(Time time, double seconds_duration);

language::ValueOrError<std::wstring> HumanReadableTime(const Time& time);

class CountDownTimer {
 public:
  CountDownTimer(double seconds);
  bool IsDone() const;

 private:
  const Time alarm_;
};

}  // namespace afc::infrastructure

bool operator==(const afc::infrastructure::Time& a,
                const afc::infrastructure::Time& b);
bool operator!=(const afc::infrastructure::Time& a,
                const afc::infrastructure::Time& b);
bool operator<(const afc::infrastructure::Time& a,
               const afc::infrastructure::Time& b);
bool operator>(const afc::infrastructure::Time& a,
               const afc::infrastructure::Time& b);
bool operator<=(const afc::infrastructure::Time& a,
                const afc::infrastructure::Time& b);
bool operator>=(const afc::infrastructure::Time& a,
                const afc::infrastructure::Time& b);

#endif  // __AFC_EDITOR_TIME_H__
