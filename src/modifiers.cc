#include "modifiers.h"

namespace afc {
namespace editor {

ostream& operator<<(ostream& os, const Modifiers& m) {
  os << "[strength: ";
  switch (m.strength) {
    case Modifiers::VERY_WEAK:
      os << "very weak";
      break;
    case Modifiers::WEAK:
      os << "weak";
      break;
    case Modifiers::DEFAULT:
      os << "default";
      break;
    case Modifiers::STRONG:
      os << "strong";
      break;
    case Modifiers::VERY_STRONG:
      os << "very strong";
      break;
  }
  os << "][direction: ";
  switch (m.direction) {
    case FORWARDS:
      os << "forwards";
      break;
    case BACKWARDS:
      os << "backwards";
      break;
  }
  os << "][default direction: ";
  switch (m.default_direction) {
    case FORWARDS:
      os << "forwards";
      break;
    case BACKWARDS:
      os << "backwards";
      break;
  }
  os << "]";
  return os;
}

}  // namespace editor
}  // namespace afc
