#ifndef __AFC_INFRASTRUCTURE_CONTROL_CHARACTER__
#define __AFC_INFRASTRUCTURE_CONTROL_CHARACTER__

#include <string>
#include <variant>
#include <vector>

#include "src/language/lazy_string/lazy_string.h"

namespace afc::infrastructure {
enum class ControlChar {
  Escape,
  DownArrow,
  UpArrow,
  LeftArrow,
  RightArrow,
  Backspace,
  PageDown,
  PageUp,
  CtrlL,
  CtrlV,
  CtrlU,
  CtrlK,
  CtrlD,
  CtrlA,
  CtrlE,
  Delete,
  Home,
  End
};

// Represents either a regular wchar_t, or a special control character.
using ExtendedChar = std::variant<wchar_t, ControlChar>;

std::vector<ExtendedChar> VectorExtendedChar(
    const language::lazy_string::LazyString&);

}  // namespace afc::infrastructure
#endif
