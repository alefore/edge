#ifndef __AFC_INFRASTRUCTURE_CONTROL_CHARACTER__
#define __AFC_INFRASTRUCTURE_CONTROL_CHARACTER__

#include <string>
#include <variant>
#include <vector>

#include "src/language/lazy_string/lazy_string.h"

namespace afc::infrastructure {
enum class ControlChar {
  kEscape,
  kDownArrow,
  kUpArrow,
  kLeftArrow,
  kRightArrow,
  kBackspace,
  kPageDown,
  kPageUp,
  kCtrlL,
  kCtrlV,
  kCtrlU,
  kCtrlK,
  kCtrlD,
  kCtrlA,
  kCtrlE,
  kDelete,
  kHome,
  kEnd
};

// Represents either a regular wchar_t, or a special control character.
using ExtendedChar = std::variant<wchar_t, ControlChar>;

std::vector<ExtendedChar> VectorExtendedChar(
    const language::lazy_string::LazyString&);

}  // namespace afc::infrastructure
#endif
