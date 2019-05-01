#include "src/find_mode.h"

#include <list>
#include <memory>
#include <string>

#include "src/buffer_variables.h"
#include "src/command_mode.h"
#include "src/editor.h"
#include "src/transformation.h"
#include "src/transformation_move.h"

namespace afc {
namespace editor {

using std::shared_ptr;
using std::unique_ptr;

class FindTransformation : public Transformation {
 public:
  FindTransformation(wchar_t c, Modifiers modifiers)
      : c_(c), modifiers_(modifiers) {}

  void Apply(OpenBuffer* buffer, Result* result) const override {
    for (size_t i = 0; i < modifiers_.repetitions; i++) {
      if (!SeekOnce(buffer, result)) {
        result->success = false;
        return;
      }
      if (buffer->Read(buffer_variables::multiple_cursors)) {
        buffer->editor()->ScheduleRedraw();
      }
      result->made_progress = true;
    }
  }

  std::unique_ptr<Transformation> Clone() const override {
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
    ColumnNumberDelta direction;
    ColumnNumberDelta times;
    ColumnNumber column = min(result->cursor.column, line->EndColumn());
    switch (modifiers_.direction) {
      case FORWARDS:
        direction = ColumnNumberDelta(1);
        times = line->EndColumn() - column;
        break;
      case BACKWARDS:
        direction = ColumnNumberDelta(-1);
        times = (column + ColumnNumberDelta(1)).ToDelta();
        break;
    }

    CHECK_GE(times.value, 0);
    for (size_t i = 1; i < static_cast<size_t>(times.value); i++) {
      if (contents->get((column + direction * i).value) == c_) {
        result->cursor.column = column + direction * i;
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
    auto buffer = editor_state->current_buffer();
    if (buffer != nullptr) {
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
