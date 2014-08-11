#ifndef __AFC_EDITOR_TERMINAL_H__
#define __AFC_EDITOR_TERMINAL_H__

#include <memory>
#include <list>
#include <string>

#include "editor.h"

namespace afc {
namespace editor {

class Terminal {
 public:
  static constexpr int DOWN_ARROW = -2;
  static constexpr int UP_ARROW = -3;
  static constexpr int LEFT_ARROW = -4;
  static constexpr int RIGHT_ARROW = -5;
  static constexpr int BACKSPACE = -6;

  Terminal();
  ~Terminal();

  void Display(EditorState* editor_state);
  void SetStatus(const std::string& status);
  int Read();

 private:
  void ShowStatus(const string& status);
  void ShowBuffer(const EditorState* editor_state);
  void AdjustPosition(const shared_ptr<OpenBuffer> buffer);

  std::string status_;
};

}  // namespace editor
}  // namespace afc

#endif
