#ifndef __AFC_EDITOR_TERMINAL_H__
#define __AFC_EDITOR_TERMINAL_H__

#include <cwchar>
#include <list>
#include <memory>
#include <string>

#include "editor.h"
#include "screen.h"

namespace afc {
namespace editor {

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

  void Display(EditorState* editor_state, Screen* screen,
               const EditorState::ScreenState& screen_state);

 private:
  void ShowStatus(const EditorState& editor_state, Screen* screen);
  void ShowBuffer(EditorState* editor_state, Screen* screen);
  void WriteLine(Screen* screen, LineNumber line,
                 OutputProducer::LineWithCursor line_with_cursor);

  void AdjustPosition(Screen* screen);

  // Position at which the cursor should be placed in the screen, if known.
  std::optional<LineColumn> cursor_position_;
};

}  // namespace editor
}  // namespace afc

#endif  // __AFC_EDITOR_TERMINAL_H__
