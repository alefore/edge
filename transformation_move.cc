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
  void Apply(
      EditorState* editor_state, OpenBuffer* buffer, Result* result) const {
    CHECK(result);
    if (buffer->current_line() == nullptr) { return; }
    buffer->CheckPosition();
    buffer->MaybeAdjustPositionCol();
    LineColumn position;
    switch (editor_state->structure()) {
      case CHAR:
        position = MoveCharacter(editor_state, buffer);
        break;
      case WORD:
        position = MoveWord(editor_state, buffer);
        break;
      default:
        CHECK(false);
    }
    LOG(INFO) << "Move to: " << position;
    NewGotoPositionTransformation(position)
        ->Apply(editor_state, buffer, result);
    if (editor_state->repetitions() > 1) {
      editor_state->PushCurrentPosition();
    }
    editor_state->ResetRepetitions();
    editor_state->ResetStructure();
    editor_state->ResetDirection();
  }

  unique_ptr<Transformation> Clone() {
    return NewMoveTransformation();
  }

 private:
  LineColumn MoveCharacter(EditorState* editor_state, OpenBuffer* buffer)
      const {
    LineColumn position = buffer->position();
    switch (editor_state->direction()) {
      case FORWARDS:
        position.column = min(position.column + editor_state->repetitions(),
            buffer->current_line()->size());
        break;
      case BACKWARDS:
        position.column -= min(position.column, editor_state->repetitions());
        break;
      default:
        CHECK(false);
    }
    return position;
  }

  static bool StringContains(const string& str, int c) {
    return str.find(static_cast<char>(c)) != string::npos;
  }

  LineColumn
  SeekToWordCharacter(OpenBuffer* buffer, Direction direction,
                      bool word_character, LineColumn position) const {
    auto line = buffer->contents()->at(position.line);

    const string& word_chars =
        buffer->read_string_variable(buffer->variable_word_characters());

    LOG(INFO) << "Seek (" << word_character << ") starting at: " << position;
    while (word_character
           ? // Seek to word-character
             (position.column == line->size()
              || !StringContains(word_chars, line->get(position.column)))
           : // Seek to non-word character.
             (position.column != line->size()
              && StringContains(word_chars, line->get(position.column)))) {
      if (direction == FORWARDS) {
        if (position.column < line->size()) {
          position.column++;
        } else if (position.line + 1 < buffer->contents()->size()) {
          position.line++;
          position.column = 0;
          line = buffer->contents()->at(position.line);
          LOG(INFO) << "Seek to next line: " << position;
        } else {
          LOG(INFO) << "Seek got to end of file.";
          return position;
        }
      } else {
        if (position.column > 0) {
          position.column--;
        } else if (position.line > 0) {
          position.line--;
          line = buffer->contents()->at(position.line);
          position.column = buffer->LineAt(position.line)->size();
          LOG(INFO) << "Seek to previous line: " << position;
        } else {
          LOG(INFO) << "Seek got to start of file.";
          return position;
        }
      }
    }

    LOG(INFO) << "Seek (" << word_character << ") stopping at: " << position;
    return position;
  }

  LineColumn MoveWord(EditorState* editor_state, OpenBuffer* buffer) const {
    LineColumn position = buffer->position();
    for (size_t i = 0; i < editor_state->repetitions(); i ++) {
      LineColumn new_position =
          SeekToWordCharacter(buffer, editor_state->direction(), true,
              SeekToWordCharacter(buffer, editor_state->direction(), false,
                  position));
      if (new_position == position) { break; }
      position = new_position;
    }
    return position;
  }
};

}  // namespace

unique_ptr<Transformation> NewMoveTransformation() {
  return unique_ptr<Transformation>(new MoveTransformation);
}

}  // namespace editor
}  // namespace afc
