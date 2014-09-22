#include "transformation_move.h"

#include <glog/logging.h>

#include "buffer.h"
#include "direction.h"
#include "editor.h"
#include "transformation.h"

namespace afc {
namespace editor {

namespace {

class MoveTransformation : public Transformation {
 public:
  unique_ptr<Transformation> Apply(
      EditorState* editor_state, OpenBuffer* buffer) const {
    buffer->CheckPosition();
    if (buffer->current_line() == nullptr) { return NewNoopTransformation(); }
    LineColumn position = buffer->position();
    switch (editor_state->direction()) {
      case FORWARDS:
        position.column = min(position.column + editor_state->repetitions(),
            buffer->current_line()->size());
        break;
      case BACKWARDS:
        position.column = min(position.column, buffer->current_line()->size());
        position.column -= min(position.column, editor_state->repetitions());
        break;
      default:
        CHECK(false);
    }
    unique_ptr<Transformation> undo(
        NewGotoPositionTransformation(position)->Apply(editor_state, buffer));
    if (editor_state->repetitions() > 1) {
      editor_state->PushCurrentPosition();
    }
    return std::move(undo);
  }

  unique_ptr<Transformation> Clone() {
    return NewMoveTransformation();
  }

  bool ModifiesBuffer() { return false; }
};

}  // namespace

unique_ptr<Transformation> NewMoveTransformation() {
  return unique_ptr<Transformation>(new MoveTransformation);
}

}  // namespace editor
}  // namespace afc
