#include "src/infrastructure/screen/line_modifier.h"

#include <glog/logging.h>

#include <ostream>

#include "src/language/containers.h"

using afc::language::InsertOrDie;

namespace afc::infrastructure::screen {
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
    for (const std::pair<const std::string, LineModifier>& entry :
         ModifierNames())
      InsertOrDie(output, {entry.second, entry.first});
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

void ToggleModifier(LineModifier m, LineModifierSet& output) {
  if (auto results = output.insert(m); !results.second)
    output.erase(results.first);
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

}  // namespace afc::infrastructure::screen
