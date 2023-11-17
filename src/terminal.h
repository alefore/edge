#ifndef __AFC_EDITOR_TERMINAL_H__
#define __AFC_EDITOR_TERMINAL_H__

#include <cwchar>
#include <list>
#include <memory>
#include <string>

#include "src/editor.h"
#include "src/infrastructure/screen/screen.h"
#include "src/line_with_cursor.h"
#include "src/lru_cache.h"

namespace afc {
namespace editor {

// Rename to something like "EditorStateDrawer" or such.
class Terminal {
 public:
  Terminal();

  // Reads the widgets' state from editor_state and writes it to screen.
  void Display(const EditorState& editor_state,
               infrastructure::screen::Screen& screen,
               const EditorState::ScreenState& screen_state);

 private:
  // Function that will draw a given line of output at the current position. It
  // also contains knowledge about where the cursor will be at the end.
  struct LineDrawer {
    std::function<void(infrastructure::screen::Screen&)> draw_callback;
    std::optional<language::lazy_string::ColumnNumber> cursor;
  };

  void WriteLine(infrastructure::screen::Screen& screen,
                 language::text::LineNumber line,
                 LineWithCursor::Generator line_with_cursor);

  // Returns a DrawLine that can be used to draw a given line.
  static LineDrawer GetLineDrawer(
      LineWithCursor line_with_cursor,
      language::lazy_string::ColumnNumberDelta width);

  void AdjustPosition(infrastructure::screen::Screen& screen);

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
