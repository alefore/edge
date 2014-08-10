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
  static const int DOWN_ARROW;
  static const int UP_ARROW;
  static const int LEFT_ARROW;
  static const int RIGHT_ARROW;

  Terminal();
  ~Terminal();

  void Display(EditorState* editor_state);
  void SetStatus(const std::string& status);
  int Read();

 private:
  void ShowStatus(const string& status);
  void ShowBuffer(const shared_ptr<OpenBuffer> buffer);
  void AdjustPosition(const shared_ptr<OpenBuffer> buffer);

  std::string status_;
};

}  // namespace editor
}  // namespace afc

#endif
