#include "src/line_modifier.h"

namespace afc {
namespace editor {

std::string ModifierToString(LineModifier modifier) {
  switch (modifier) {
    case RESET: return "RESET";
    case BOLD: return "BOLD";
    case DIM: return "DIM";
    case UNDERLINE: return "UNDERLINE";
    case REVERSE: return "REVERSE";
    case BLACK: return "BLACK";
    case RED: return "RED";
    case GREEN: return "GREEN";
    case BLUE: return "BLUE";
    case CYAN: return "CYAN";
    case YELLOW: return "YELLOW";
    case MAGENTA: return "MAGENTA";
    case BG_RED: return "BG_RED";
  }
  return "UNKNOWN";
}

LineModifier ModifierFromString(std::string modifier) {
  // TODO: Turn into a map.
  if (modifier == "RESET") return RESET;
  if (modifier == "BOLD") return BOLD;
  if (modifier == "DIM") return DIM;
  if (modifier == "UNDERLINE") return UNDERLINE;
  if (modifier == "REVERSE") return REVERSE;
  if (modifier == "BLACK") return BLACK;
  if (modifier == "RED") return RED;
  if (modifier == "GREEN") return GREEN;
  if (modifier == "BLUE") return BLUE;
  if (modifier == "CYAN") return CYAN;
  if (modifier == "YELLOW") return YELLOW;
  if (modifier == "MAGENTA") return MAGENTA;
  if (modifier == "BG_RED") return BG_RED;
  return RESET;  // Ugh.
}

}  // namespace editor
}  // namespace afc
