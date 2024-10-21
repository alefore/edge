#ifndef __AFC_EDITOR_SRC_INFRASTRUCTURE_TIME_HUMAN_H__
#define __AFC_EDITOR_SRC_INFRASTRUCTURE_TIME_HUMAN_H__

#include "src/infrastructure/time.h"
#include "src/language/error/value_or_error.h"
#include "src/language/lazy_string/lazy_string.h"
#include "src/language/lazy_string/single_line.h"

namespace afc::infrastructure {
language::ValueOrError<language::lazy_string::NonEmptySingleLine>
HumanReadableTime(const Time& time);
}

#endif  // __AFC_EDITOR_SRC_INFRASTRUCTURE_TIME_HUMAN_H__
