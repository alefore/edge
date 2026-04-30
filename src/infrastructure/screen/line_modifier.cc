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
      {NON_EMPTY_SINGLE_LINE_CONSTANT(L"RESET"), LineModifier::kReset},
      {NON_EMPTY_SINGLE_LINE_CONSTANT(L"BOLD"), LineModifier::kBold},
      {NON_EMPTY_SINGLE_LINE_CONSTANT(L"ITALIC"), LineModifier::kItalic},
      {NON_EMPTY_SINGLE_LINE_CONSTANT(L"DIM"), LineModifier::kDim},
      {NON_EMPTY_SINGLE_LINE_CONSTANT(L"UNDERLINE"), LineModifier::kUnderline},
      {NON_EMPTY_SINGLE_LINE_CONSTANT(L"REVERSE"), LineModifier::kReverse},
      {NON_EMPTY_SINGLE_LINE_CONSTANT(L"BLACK"), LineModifier::kBlack},
      {NON_EMPTY_SINGLE_LINE_CONSTANT(L"RED"), LineModifier::kRed},
      {NON_EMPTY_SINGLE_LINE_CONSTANT(L"GREEN"), LineModifier::kGreen},
      {NON_EMPTY_SINGLE_LINE_CONSTANT(L"BLUE"), LineModifier::kBlue},
      {NON_EMPTY_SINGLE_LINE_CONSTANT(L"CYAN"), LineModifier::kCyan},
      {NON_EMPTY_SINGLE_LINE_CONSTANT(L"YELLOW"), LineModifier::kYellow},
      {NON_EMPTY_SINGLE_LINE_CONSTANT(L"MAGENTA"), LineModifier::kMagenta},
      {NON_EMPTY_SINGLE_LINE_CONSTANT(L"BG_RED"), LineModifier::kBgRed}};
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

}  // namespace afc::infrastructure::screen
