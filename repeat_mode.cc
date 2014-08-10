#include "repeat_mode.h"

#include <memory>

#include "command_mode.h"
#include "editor.h"

namespace {
using namespace afc::editor;

class RepeatMode : public EditorMode {
  void ProcessInput(int c, EditorState* editor_state) {
    if (c >= '0' && c <= '9') {
      editor_state->repetitions = 10 * editor_state->repetitions + c - '0';
      return;
    }

    editor_state->mode = std::move(NewCommandMode());
    editor_state->mode->ProcessInput(c, editor_state);
  }
};
}

namespace afc {
namespace editor {

using std::unique_ptr;

unique_ptr<EditorMode> NewRepeatMode() {
  return unique_ptr<EditorMode>(new RepeatMode());
}

}  // namespace afc
}  // namespace editor
