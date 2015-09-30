#include <memory>
#include <list>
#include <string>

#include "command_mode.h"
#include "find_mode.h"
#include "editor.h"

namespace afc {
namespace editor {

using std::unique_ptr;
using std::shared_ptr;

class FindMode : public EditorMode {
 public:
  bool SeekOnce(Direction direction, const shared_ptr<OpenBuffer> buffer, int c) {
    if (buffer->current_line() == nullptr) { return false; }
    shared_ptr<LazyString> current_line = buffer->current_line()->contents();
    int delta;
    size_t position = buffer->current_position_col();
    size_t times;
    assert(position <= current_line->size());
    switch (direction) {
      case FORWARDS:
        delta = 1;
        times = current_line->size() - position;
        break;
      case BACKWARDS:
        delta = -1;
        times = position + 1;
        break;
      default:
        assert(false);
    }

    if (times == 0) {
      return false;
    }
    times --;
    for (size_t i = 0; i < times; i ++) {
      position += delta;
      if (current_line->get(position) == c) {
        buffer->set_current_position_col(position);
        return true;
      }
    }
    return false;
  }

  void ProcessInput(wint_t c, EditorState* editor_state) {
    editor_state->PushCurrentPosition();
    if (editor_state->has_current_buffer()) {
      auto buffer = editor_state->current_buffer()->second;
      buffer->CheckPosition();
      buffer->MaybeAdjustPositionCol();
      for (size_t times = 0; times < editor_state->repetitions(); times++) {
        if (!SeekOnce(editor_state->direction(),
                      editor_state->current_buffer()->second, c)) {
          break;
        }
      }
    }
    editor_state->ResetMode();
    editor_state->ResetRepetitions();
    editor_state->ResetDirection();
  }
};

unique_ptr<EditorMode> NewFindMode() {
  return std::move(unique_ptr<EditorMode>(new FindMode()));
}

}  // namespace afc
}  // namespace editor
