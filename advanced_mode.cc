#include <memory>
#include <list>
#include <string>

#include "advanced_mode.h"
#include "command_mode.h"
#include "editor.h"

namespace afc {
namespace editor {

using std::unique_ptr;
using std::shared_ptr;

class AdvancedMode : public EditorMode {
  void WriteBuffer(shared_ptr<OpenBuffer>& buffer) {
  }

  void CloseCurrentBuffer(EditorState* editor_state) {
    editor_state->buffers.erase(
        editor_state->buffers.begin()
        + editor_state->current_buffer);
    editor_state->current_buffer = 0;
  }

  void ProcessInput(int c, EditorState* editor_state) {
    shared_ptr<OpenBuffer> buffer(editor_state->get_current_buffer());

    switch (c) {
      case 'w':
        WriteBuffer(buffer);
        break;
      case 'c':
        CloseCurrentBuffer(editor_state);
        break;
    }

    editor_state->mode = std::move(NewCommandMode());
    editor_state->repetitions = 1;
  }
};

unique_ptr<EditorMode> NewAdvancedMode() {
  return std::move(unique_ptr<EditorMode>(new AdvancedMode()));
}

}  // namespace afc
}  // namespace editor
