#ifndef __AFC_EDITOR_TERMINAL_H__
#define __AFC_EDITOR_TERMINAL_H__

#include <cwchar>
#include <list>
#include <memory>
#include <string>

#include "src/editor.h"
#include "src/line_with_cursor.h"
#include "src/lru_cache.h"
#include "src/screen.h"

namespace afc {
namespace editor {

// Rename to something like "EditorStateDrawer" or such.
class Terminal {
 public:
  static constexpr int ESCAPE = -2;
  static constexpr int DOWN_ARROW = -3;
  static constexpr int UP_ARROW = -4;
  static constexpr int LEFT_ARROW = -5;
  static constexpr int RIGHT_ARROW = -6;
  static constexpr int BACKSPACE = -7;
  static constexpr int PAGE_DOWN = -8;
  static constexpr int PAGE_UP = -9;
  static constexpr int CTRL_L = -10;
  static constexpr int CTRL_V = -11;
  static constexpr int CTRL_U = -12;
  static constexpr int CTRL_K = -13;
  static constexpr int CTRL_D = -14;
  static constexpr int CTRL_A = -15;
  static constexpr int CTRL_E = -16;
  static constexpr int DELETE = -17;

  Terminal();

  // Reads the widgets' state from editor_state and writes it to screen.
  void Display(const EditorState& editor_state, Screen& screen,
               const EditorState::ScreenState& screen_state);

 private:
  // Function that will draw a given line of output at the current position. It
  // also contains knowledge about where the cursor will be at the end.
  struct LineDrawer {
    std::function<void(Screen&)> draw_callback;
    std::optional<language::lazy_string::ColumnNumber> cursor;
  };

  void WriteLine(Screen& screen, language::text::LineNumber line,
                 LineWithCursor::Generator line_with_cursor);

  // Returns a DrawLine that can be used to draw a given line.
  static LineDrawer GetLineDrawer(
      LineWithCursor line_with_cursor,
      language::lazy_string::ColumnNumberDelta width);

  void AdjustPosition(Screen& screen);

  // Position at which the cursor should be placed in the screen, if known.
  std::optional<language::text::LineColumn> cursor_position_;

  // Value at position i is the hash of the line currently drawn at line i, if
  // known.
  std::vector<std::optional<size_t>> hashes_current_lines_;

  // Given the hash of a line, return a LineDrawer that can be used to draw it.
  LRUCache<size_t, LineDrawer> lines_cache_;
};

}  // namespace editor
}  // namespace afc

#endif  // __AFC_EDITOR_TERMINAL_H__
