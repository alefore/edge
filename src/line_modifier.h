#ifndef __AFC_EDITOR_LINE_MODIFIER_H__
#define __AFC_EDITOR_LINE_MODIFIER_H__

#include <unordered_set>

#include "src/language/hash.h"
#include "src/vm/public/environment.h"

namespace afc {
namespace editor {

enum LineModifier {
  RESET,
  BOLD,
  ITALIC,
  DIM,
  UNDERLINE,
  REVERSE,
  BLACK,
  RED,
  GREEN,
  BLUE,
  CYAN,
  YELLOW,
  MAGENTA,
  WHITE,
  BG_RED,
};

using LineModifierSet = std::unordered_set<LineModifier, std::hash<int>>;

std::string ModifierToString(LineModifier modifier);
LineModifier ModifierFromString(std::string modifier);

std::ostream& operator<<(std::ostream& os, const LineModifierSet& s);

}  // namespace editor
}  // namespace afc

#endif  // __AFC_EDITOR_LINE_MODIFIER_H__
