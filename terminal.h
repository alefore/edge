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
  Terminal();
  ~Terminal();

  void Display(EditorState* editor_state);
  void SetStatus(const std::string& status);
  int Read();

 private:
  void ShowBuffer(const shared_ptr<OpenBuffer> buffer);

  std::string status_;
};

}  // namespace editor
}  // namespace afc

#endif
