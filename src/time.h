#ifndef __AFC_EDITOR_TIME_H__
#define __AFC_EDITOR_TIME_H__

#include <functional>
#include <memory>
#include <string>
#include <unordered_set>
#include <vector>

#include "src/lazy_string.h"
#include "src/line_column.h"
#include "src/line_modifier.h"

namespace afc {
namespace editor {
double MillisecondsBetween(const struct timespec& begin,
                           const struct timespec& end);

double GetElapsedMillisecondsSince(const struct timespec& spec);

double GetElapsedMillisecondsAndUpdate(struct timespec* spec);

// If required_milliseconds have passed since the last call, updates `spec` to
// now and returns the elapsed time. Otherwise, returns nullopt.
std::optional<double> UpdateIfMillisecondsHavePassed(
    struct timespec* spec, double required_milliseconds);
}  // namespace editor
}  // namespace afc

#endif  // __AFC_EDITOR_TIME_H__
