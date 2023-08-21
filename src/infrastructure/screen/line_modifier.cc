#include "src/infrastructure/screen/line_modifier.h"

#include <glog/logging.h>

#include <ostream>

namespace afc {
namespace editor {

const std::unordered_map<std::string, LineModifier>& ModifierNames() {
  static const std::unordered_map<std::string, LineModifier> values = {
      {"RESET", LineModifier::kReset},
      {"BOLD", LineModifier::kBold},
      {"ITALIC", LineModifier::kItalic},
      {"DIM", LineModifier::kDim},
      {"UNDERLINE", LineModifier::kUnderline},
      {"REVERSE", LineModifier::kReverse},
      {"BLACK", LineModifier::kBlack},
      {"RED", LineModifier::kRed},
      {"GREEN", LineModifier::kGreen},
      {"BLUE", LineModifier::kBlue},
      {"CYAN", LineModifier::kCyan},
      {"YELLOW", LineModifier::kYellow},
      {"MAGENTA", LineModifier::kMagenta},
      {"BG_RED", LineModifier::kBgRed}};
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
  return LineModifier::kReset;  // Ugh.
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
