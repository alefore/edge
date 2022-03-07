#include "src/line_modifier.h"

namespace afc {
namespace editor {

std::string ModifierToString(LineModifier modifier) {
  switch (modifier) {
    case RESET:
      return "RESET";
    case BOLD:
      return "BOLD";
    case ITALIC:
      return "ITALIC";
    case DIM:
      return "DIM";
    case UNDERLINE:
      return "UNDERLINE";
    case REVERSE:
      return "REVERSE";
    case BLACK:
      return "BLACK";
    case RED:
      return "RED";
    case GREEN:
      return "GREEN";
    case BLUE:
      return "BLUE";
    case CYAN:
      return "CYAN";
    case YELLOW:
      return "YELLOW";
    case MAGENTA:
      return "MAGENTA";
    case BG_RED:
      return "BG_RED";
  }
  return "UNKNOWN";
}

LineModifier ModifierFromString(std::string modifier) {
  static const std::unordered_map<std::string, LineModifier> values = {
      {"RESET", RESET},         {"BOLD", BOLD},
      {"ITALIC", ITALIC},       {"DIM", DIM},
      {"UNDERLINE", UNDERLINE}, {"REVERSE", REVERSE},
      {"BLACK", BLACK},         {"RED", RED},
      {"GREEN", GREEN},         {"BLUE", BLUE},
      {"CYAN", CYAN},           {"YELLOW", YELLOW},
      {"MAGENTA", MAGENTA},     {"BG_RED", BG_RED}};
  if (auto it = values.find(modifier); it != values.end()) return it->second;
  return RESET;  // Ugh.
}

}  // namespace editor
}  // namespace afc
