#include <memory>
#include <list>
#include <string>

#include "command_mode.h"
#include "find_mode.h"

namespace afc {
namespace editor {

using std::unique_ptr;

class CommandMode : public EditorMode {
  void ProcessInput(int c, EditorState* editor_state) {
    shared_ptr<OpenBuffer> buffer =
        editor_state->buffers[editor_state->current_buffer];
    switch (c) {
      case 'q':
        editor_state->terminate = true;
        break;
      case 'j':
        if (buffer->current_position_line < buffer->contents.size() - 1) {
          buffer->current_position_line++;
        }
        break;
      case 'k':
        if (buffer->current_position_line > 0) {
          buffer->current_position_line--;
        }
        break;
      case 'l':
        if (buffer->current_position_col < buffer->current_line()->size()) {
          buffer->current_position_col++;
        } else if (buffer->current_position_col > buffer->current_line()->size()) {
          buffer->current_position_col = buffer->current_line()->size();
        }
        break;
      case 'h':
        if (buffer->current_position_col > buffer->current_line()->size()) {
          buffer->current_position_col = buffer->current_line()->size();
        }
        if (buffer->current_position_col > 0) {
          buffer->current_position_col--;
        }
        break;
      case 'f':
        editor_state->mode = std::move(NewFindMode());
        break;
    }
  }
};

unique_ptr<EditorMode> NewCommandMode() {
  return std::move(unique_ptr<EditorMode>(new CommandMode()));
}

}  // namespace afc
}  // namespace editor
