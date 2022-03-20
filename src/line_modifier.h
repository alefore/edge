#ifndef __AFC_EDITOR_LINE_MODIFIER_H__
#define __AFC_EDITOR_LINE_MODIFIER_H__

#include "src/hash.h"
#include "src/lazy_string.h"
#include "src/vm/public/environment.h"
#include "unordered_set"

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

}  // namespace editor
}  // namespace afc

#endif  // __AFC_EDITOR_LINE_MODIFIER_H__
