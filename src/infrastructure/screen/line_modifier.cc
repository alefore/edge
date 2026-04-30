#include "src/infrastructure/screen/line_modifier.h"

#include <glog/logging.h>

#include <ostream>

#include "src/language/container.h"
#include "src/language/wstring.h"

using afc::language::Error;
using afc::language::FromByteString;
using afc::language::InsertOrDie;
using afc::language::lazy_string::LazyString;
using afc::language::lazy_string::NonEmptySingleLine;

namespace afc::infrastructure::screen {
const std::unordered_map<NonEmptySingleLine, LineModifier>& LineModifiers() {
  static const std::unordered_map<NonEmptySingleLine, LineModifier> values = {
      {NON_EMPTY_SINGLE_LINE_CONSTANT(L"RESET"), LineModifier::Reset},
      {NON_EMPTY_SINGLE_LINE_CONSTANT(L"BOLD"), LineModifier::Bold},
      {NON_EMPTY_SINGLE_LINE_CONSTANT(L"ITALIC"), LineModifier::Italic},
      {NON_EMPTY_SINGLE_LINE_CONSTANT(L"DIM"), LineModifier::Dim},
      {NON_EMPTY_SINGLE_LINE_CONSTANT(L"UNDERLINE"), LineModifier::Underline},
      {NON_EMPTY_SINGLE_LINE_CONSTANT(L"REVERSE"), LineModifier::Reverse},
      {NON_EMPTY_SINGLE_LINE_CONSTANT(L"BLACK"), LineModifier::Black},
      {NON_EMPTY_SINGLE_LINE_CONSTANT(L"RED"), LineModifier::Red},
      {NON_EMPTY_SINGLE_LINE_CONSTANT(L"GREEN"), LineModifier::Green},
      {NON_EMPTY_SINGLE_LINE_CONSTANT(L"BLUE"), LineModifier::Blue},
      {NON_EMPTY_SINGLE_LINE_CONSTANT(L"CYAN"), LineModifier::Cyan},
      {NON_EMPTY_SINGLE_LINE_CONSTANT(L"YELLOW"), LineModifier::Yellow},
      {NON_EMPTY_SINGLE_LINE_CONSTANT(L"MAGENTA"), LineModifier::Magenta},
      {NON_EMPTY_SINGLE_LINE_CONSTANT(L"BG_RED"), LineModifier::BgRed}};
  return values;
}

NonEmptySingleLine ModifierToString(LineModifier modifier) {
  static const std::unordered_map<LineModifier, NonEmptySingleLine> values =
      LineModifiers() |
      std::views::transform(
          [](const std::pair<NonEmptySingleLine, LineModifier> data) {
            return std::make_pair(data.second, data.first);
          }) |
      std::ranges::to<std::unordered_map>();
  return GetValueOrDie(values, modifier);
}

std::expected<LineModifier, Error> ModifierFromString(
    NonEmptySingleLine modifier) {
  const std::unordered_map<NonEmptySingleLine, LineModifier>& values =
      LineModifiers();
  if (auto it = values.find(modifier); it != values.end()) return it->second;
  return Error{LazyString{L"Unknown modifier: "} + modifier};
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

LineModifierSet HashToModifiers(int hash_value,
                                HashToModifiersBold bold_behavior) {
  LineModifierSet output;
  static std::vector<LineModifier> modifiers = {
      LineModifier::Cyan, LineModifier::Yellow, LineModifier::Red,
      LineModifier::Blue, LineModifier::Green,  LineModifier::Magenta};
  output.insert(modifiers[hash_value % modifiers.size()]);
  if (bold_behavior == HashToModifiersBold::Sometimes &&
      ((hash_value / modifiers.size()) % 2) == 0)
    output.insert(LineModifier::Bold);
  return output;
}
}  // namespace afc::infrastructure::screen
