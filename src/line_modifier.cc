#include "src/line_modifier.h"

#include <glog/logging.h>

namespace afc {
namespace editor {

const std::unordered_map<std::string, LineModifier>& ModifierNames() {
  static const std::unordered_map<std::string, LineModifier> values = {
      {"RESET", RESET},         {"BOLD", BOLD},
      {"ITALIC", ITALIC},       {"DIM", DIM},
      {"UNDERLINE", UNDERLINE}, {"REVERSE", REVERSE},
      {"BLACK", BLACK},         {"RED", RED},
      {"GREEN", GREEN},         {"BLUE", BLUE},
      {"CYAN", CYAN},           {"YELLOW", YELLOW},
      {"MAGENTA", MAGENTA},     {"BG_RED", BG_RED}};
  return values;
}

std::string ModifierToString(LineModifier modifier) {
  static const std::unordered_map<LineModifier, std::string> values = [] {
    std::unordered_map<LineModifier, std::string> output;
    for (const auto& it : ModifierNames()) {
      std::string name = it.first;
      auto insert_result = output.insert({it.second, it.first}).second;
      CHECK(insert_result);
    }
    return output;
  }();

  if (auto it = values.find(modifier); it != values.end()) return it->second;
  return "UNKNOWN";
}

LineModifier ModifierFromString(std::string modifier) {
  const std::unordered_map<std::string, LineModifier>& values = ModifierNames();
  if (auto it = values.find(modifier); it != values.end()) return it->second;
  return RESET;  // Ugh.
}

std::ostream& operator<<(std::ostream& os, const LineModifierSet& s) {
  std::string separator;
  os << "{";
  for (const auto& m : s) {
    os << separator << ModifierToString(m);
    separator = ", ";
  }
  os << "}";
  return os;
}

}  // namespace editor
}  // namespace afc
