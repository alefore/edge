#ifndef __AFC_EDITOR_INFRASTRUCTURE_LINE_MODIFIER_H__
#define __AFC_EDITOR_INFRASTRUCTURE_LINE_MODIFIER_H__

#include <functional>
#include <string>
#include <unordered_set>

#include "src/language/error/value_or_error.h"
#include "src/language/hash.h"
#include "src/language/lazy_string/lazy_string.h"
#include "src/language/lazy_string/single_line.h"

namespace afc::infrastructure::screen {
// TODO(trivial, 2026-04-30, enum class): Drop the `k` prefix.
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

const std::unordered_map<language::lazy_string::NonEmptySingleLine,
                         LineModifier>&
LineModifiers();

language::lazy_string::NonEmptySingleLine ModifierToString(
    LineModifier modifier);
std::expected<LineModifier, language::Error> ModifierFromString(
    language::lazy_string::NonEmptySingleLine modifier);

void ToggleModifier(LineModifier m, LineModifierSet& output);

enum class HashToModifiersBold { Sometimes, Never };

LineModifierSet HashToModifiers(int hash_value,
                                HashToModifiersBold bold_behavior);
std::ostream& operator<<(std::ostream& os, const LineModifierSet& s);

}  // namespace afc::infrastructure::screen

#endif  // __AFC_EDITOR_INFRASTRUCTURE_LINE_MODIFIER_H__
