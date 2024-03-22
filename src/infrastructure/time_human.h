#ifndef __AFC_EDITOR_SRC_INFRASTRUCTURE_TIME_HUMAN_H__
#define __AFC_EDITOR_SRC_INFRASTRUCTURE_TIME_HUMAN_H__

#include <string>

#include "src/infrastructure/time.h"

namespace afc::infrastructure {
language::ValueOrError<std::wstring> HumanReadableTime(const Time& time);
}

#endif  // __AFC_EDITOR_SRC_INFRASTRUCTURE_TIME_HUMAN_H__
