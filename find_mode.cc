#include <memory>
#include <list>
#include <string>

#include "command_mode.h"
#include "editor.h"

namespace afc {
namespace editor {

using std::unique_ptr;
using std::shared_ptr;

class FindMode : public EditorMode {

  void SeekOnce(shared_ptr<OpenBuffer>& buffer, int c) {
    shared_ptr<LazyString> current_line = buffer->current_line()->contents;
    size_t line_length = current_line->size();
    for (int i = buffer->current_position_col + 1; i < line_length; i++) {
      if (current_line->get(i) == c) {
        buffer->current_position_col = i;
        return;
      }
    }
  }

  void ProcessInput(int c, EditorState* editor_state) {
    for (int times = 0; times < editor_state->repetitions; times++) {
      SeekOnce(editor_state->buffers[editor_state->current_buffer], c);
    }
    editor_state->mode = std::move(NewCommandMode());
    editor_state->repetitions = 1;
  }
};

unique_ptr<EditorMode> NewFindMode() {
  return std::move(unique_ptr<EditorMode>(new FindMode()));
}

}  // namespace afc
}  // namespace editor
