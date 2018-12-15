#include <list>
#include <memory>
#include <string>

#include "command_mode.h"
#include "editor.h"
#include "find_mode.h"
#include "transformation.h"
#include "transformation_move.h"

namespace afc {
namespace editor {

using std::shared_ptr;
using std::unique_ptr;

class FindTransformation : public Transformation {
 public:
  FindTransformation(wchar_t c, Modifiers modifiers)
      : c_(c), modifiers_(modifiers) {}

  void Apply(EditorState* editor, OpenBuffer* buffer,
             Result* result) const override {
    for (size_t i = 0; i < modifiers_.repetitions; i++) {
      if (!SeekOnce(buffer, result)) {
        result->success = false;
        break;
      } else {
        editor->ScheduleRedraw();  // TODO: Only if multiple cursors.
        result->made_progress = true;
      }
    }
  }

  std::unique_ptr<Transformation> Clone() {
    return std::make_unique<FindTransformation>(c_, modifiers_);
  }

 private:
  bool SeekOnce(OpenBuffer* buffer, Result* result) const {
    auto line = buffer->LineAt(result->cursor.line);
    if (line == nullptr) {
      return false;
    }
    shared_ptr<LazyString> contents = line->contents();
    CHECK(contents != nullptr);
    int direction = 1;
    size_t times = 0;
    size_t position = min(result->cursor.column, contents->size());
    switch (modifiers_.direction) {
      case FORWARDS:
        direction = 1;
        times = contents->size() - position;
        break;
      case BACKWARDS:
        direction = -1;
        times = position + 1;
        break;
    }

    for (size_t i = 1; i < times; i++) {
      if (contents->get(position + direction * i) == c_) {
        result->cursor.column = position + direction * i;
        return true;
      }
    }
    return false;
  }

  const wchar_t c_;
  const Modifiers modifiers_;
};

class FindMode : public EditorMode {
  void ProcessInput(wint_t c, EditorState* editor_state) {
    editor_state->PushCurrentPosition();
    if (editor_state->has_current_buffer()) {
      auto buffer = editor_state->current_buffer()->second;
      buffer->ApplyToCursors(
          std::make_unique<FindTransformation>(c, editor_state->modifiers()));
      buffer->ResetMode();
    }
    editor_state->ResetRepetitions();
    editor_state->ResetDirection();
  }
};

std::unique_ptr<EditorMode> NewFindMode() {
  return std::make_unique<FindMode>();
}

}  // namespace editor
}  // namespace afc
