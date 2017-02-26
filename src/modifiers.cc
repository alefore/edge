#include "modifiers.h"

#include "wstring.h"

namespace afc {
namespace editor {

std::ostream& operator<<(std::ostream& os, const BufferPosition& bp) {
  os << "[" << bp.buffer_name << ":" << bp.position << "]";
  return os;
}

ostream& operator<<(ostream& os, const Modifiers& m) {
  os << "[structure: ";
  switch (m.structure) {
    case CHAR:
      os << "CHAR";
      break;
    case WORD:
      os << "WORD";
      break;
    case LINE:
      os << "LINE";
      break;
    case MARK:
      os << "MARK";
      break;
    case PAGE:
      os << "PAGE";
      break;
    case SEARCH:
      os << "SEARCH";
      break;
    case TREE:
      os << "TREE";
      break;
    case CURSOR:
      os << "CURSOR";
      break;
    case BUFFER:
      os << "BUFFER";
      break;
  }
  os << "][strength: ";
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
  os << "][repetitions: " << m.repetitions << "]";
  return os;
}

}  // namespace editor
}  // namespace afc
