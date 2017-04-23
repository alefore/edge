#include "modifiers.h"

#include "wstring.h"

namespace afc {
namespace editor {

std::ostream& operator<<(std::ostream& os, const BufferPosition& bp) {
  os << "[" << bp.buffer_name << ":" << bp.position << "]";
  return os;
}

wstring StructureToString(Structure structure) {
  switch (structure) {
    case CHAR:
      return L"char";
    case WORD:
      return L"word";
    case LINE:
      return L"line";
    case MARK:
      return L"mark";
    case PAGE:
      return L"page";
    case SEARCH:
      return L"search";
    case TREE:
      return L"tree";
    case CURSOR:
      return L"cursor";
    case BUFFER:
      return L"buffer";
  }
  CHECK(false);
}

ostream& operator<<(ostream& os, const Modifiers& m) {
  os << "[structure: " << StructureToString(m.structure) << "][strength: ";
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

Modifiers::Boundary IncrementBoundary(Modifiers::Boundary boundary) {
  switch (boundary) {
    case Modifiers::CURRENT_POSITION:
      return Modifiers::LIMIT_CURRENT;
    case Modifiers::LIMIT_CURRENT:
      return Modifiers::LIMIT_NEIGHBOR;
    case Modifiers::LIMIT_NEIGHBOR:
      return Modifiers::CURRENT_POSITION;
  }
  CHECK(false);
}

}  // namespace editor
}  // namespace afc
