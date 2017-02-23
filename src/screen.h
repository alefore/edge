#ifndef __AFC_EDITOR_SCREEN_H__
#define __AFC_EDITOR_SCREEN_H__

#include "line.h"

namespace afc {
namespace editor {

class Screen {
 public:
  Screen() = default;
  virtual ~Screen() = default;

  virtual void HardRefresh() = 0;
  virtual void Refresh() = 0;
  virtual void Clear() = 0;

  enum CursorVisibility {
    INVISIBLE,
    NORMAL,
  };

  static string CursorVisibilityToString(CursorVisibility cursor_visibility) {
    switch (cursor_visibility) {
      case INVISIBLE: return "INVISIBLE";
      case NORMAL: return "NORMAL";
    }
    return "UNKNOWN";
  }

  static CursorVisibility CursorVisibilityFromString(string cursor_visibility) {
    if (cursor_visibility == "NORMAL") return NORMAL;
    if (cursor_visibility == "INVISIBLE") return INVISIBLE;
    return NORMAL;  // Ugh.
  }

  virtual void SetCursorVisibility(CursorVisibility cursor_visibility) = 0;
  virtual void Move(size_t y, size_t x) = 0;
  virtual void WriteString(const wstring& str) = 0;

  virtual void SetModifier(Line::Modifier modifier) = 0;

  virtual size_t columns() const = 0;
  virtual size_t lines() const = 0;
};

}  // namespace afc
}  // namespace editor

#endif  // __AFC_EDITOR_SCREEN_H__
