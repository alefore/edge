#ifndef __AFC_EDITOR_INFRASTRUCTURE_LINE_MODIFIER_H__
#define __AFC_EDITOR_INFRASTRUCTURE_LINE_MODIFIER_H__

#include <functional>
#include <string>
#include <unordered_set>

#include "src/language/hash.h"

namespace afc::infrastructure::screen {
enum class LineModifier {
  kReset,
  kBold,
  kItalic,
  kDim,
  kUnderline,
  kReverse,
  kBlack,
  kRed,
  kGreen,
  kBlue,
  kCyan,
  kYellow,
  kMagenta,
  kWhite,
  kBgRed,
};

using LineModifierSet =
    std::unordered_set<LineModifier, language::EnumClassHash>;

std::string ModifierToString(LineModifier modifier);
LineModifier ModifierFromString(std::string modifier);

void ToggleModifier(LineModifier m, LineModifierSet& output);

std::ostream& operator<<(std::ostream& os, const LineModifierSet& s);

}  // namespace afc::infrastructure::screen

#endif  // __AFC_EDITOR_INFRASTRUCTURE_LINE_MODIFIER_H__
