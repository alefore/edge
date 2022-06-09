#ifndef __AFC_EDITOR_SCREEN_H__
#define __AFC_EDITOR_SCREEN_H__

#include "src/line.h"
#include "src/line_column.h"

namespace afc {
namespace editor {

class Screen {
 public:
  Screen() = default;
  virtual ~Screen() = default;

  // Most implementations apply their transformations directly. However, there's
  // an implementation that buffers them until Flush is called and then applies
  // them all at once. This is useful for client Edge instances that receive
  // their updates gradually, to ensure that they can always refresh the screen,
  // which allows them to detect window resizes immediately, knowing that they
  // won't be publishing an incomplete update (being flushed from the server).
  virtual void Flush() = 0;

  virtual void HardRefresh() = 0;
  virtual void Refresh() = 0;
  virtual void Clear() = 0;

  enum CursorVisibility {
    INVISIBLE,
    NORMAL,
  };

  static std::string CursorVisibilityToString(
      CursorVisibility cursor_visibility) {
    switch (cursor_visibility) {
      case INVISIBLE:
        return "INVISIBLE";
      case NORMAL:
        return "NORMAL";
    }
    LOG(WARNING) << "Invalid cursor visibility: " << cursor_visibility;
    return "UNKNOWN";
  }

  static CursorVisibility CursorVisibilityFromString(
      std::string cursor_visibility) {
    if (cursor_visibility == "NORMAL") return NORMAL;
    if (cursor_visibility == "INVISIBLE") return INVISIBLE;

    LOG(WARNING) << "Invalid cursor visibility: " << cursor_visibility;
    return NORMAL;
  }

  virtual void SetCursorVisibility(CursorVisibility cursor_visibility) = 0;
  virtual void Move(LineColumn position) = 0;
  virtual void WriteString(
      const language::NonNull<
          std::shared_ptr<language::lazy_string::LazyString>>& str) = 0;

  virtual void SetModifier(LineModifier modifier) = 0;

  virtual LineColumnDelta size() const = 0;
};

}  // namespace editor
}  // namespace afc

#endif  // __AFC_EDITOR_SCREEN_H__
