#include "transformation_delete.h"

#include <glog/logging.h>

#include "buffer.h"
#include "direction.h"
#include "editor.h"
#include "lazy_string_append.h"
#include "transformation.h"
#include "transformation_move.h"

namespace afc {
namespace editor {

void InsertDeletedTextBuffer(EditorState* editor_state,
                             const shared_ptr<OpenBuffer>& buffer) {
  auto insert_result = editor_state->buffers()->insert(
      make_pair(OpenBuffer::kPasteBuffer, buffer));
  if (!insert_result.second) {
    insert_result.first->second = buffer;
  }
}

namespace {
class DeleteCharactersTransformation : public Transformation {
 public:
  DeleteCharactersTransformation(size_t repetitions, bool copy_to_paste_buffer)
      : repetitions_(repetitions),
        copy_to_paste_buffer_(copy_to_paste_buffer) {}

  void Apply(
      EditorState* editor_state, OpenBuffer* buffer, Result* result) const {
    buffer->CheckPosition();
    buffer->MaybeAdjustPositionCol();
    shared_ptr<OpenBuffer> deleted_text(
        new OpenBuffer(editor_state, OpenBuffer::kPasteBuffer));
    bool needs_empty_line = false;
    size_t characters_erased = 0;
    while (characters_erased < repetitions_) {
      LOG(INFO) << "Erased " << characters_erased << " of " << repetitions_;
      if (buffer->line() == buffer->line_end()) {
        LOG(INFO) << "Giving up: at end of file.";
        characters_erased = repetitions_;
        result->success = false;
        continue;
      }
      size_t characters_left =
          (*buffer->line())->size() - buffer->current_position_col();
      if (repetitions_ - characters_erased <= characters_left
          || buffer->line().line() + 1 == buffer->line_end().line()) {
        size_t characters_to_erase = min(
            (*buffer->line())->size() - buffer->current_position_col(),
            repetitions_ - characters_erased);
        result->success =
            characters_to_erase == repetitions_ - characters_erased;
        LOG(INFO) << "Maybe not enough characters left ("
                  << characters_to_erase << ")";
        characters_erased = repetitions_;
        deleted_text->AppendLine(editor_state,
            (*buffer->line())->Substring(
                buffer->current_position_col(), characters_to_erase));
        buffer->contents()->at(buffer->line().line()).reset(
            new Line(Line::Options(StringAppend(
                (*buffer->line())->Substring(0, buffer->current_position_col()),
                (*buffer->line())->Substring(
                    buffer->current_position_col() + characters_to_erase)))));
        result->modified_buffer = true;
        needs_empty_line = false;
        continue;
      }
      characters_erased += characters_left + 1;
      LOG(INFO) << "Erasing characters " << characters_left + 1 << " from line "
                << buffer->line().line();
      deleted_text->AppendLine(editor_state,
          (*buffer->line())->Substring(buffer->current_position_col()));
      LOG(INFO) << "Overwriting line: " << buffer->line().line();
      buffer->contents()->at(buffer->line().line()).reset(
          new Line(Line::Options(StringAppend(
              (*buffer->line())->Substring(0, buffer->current_position_col()),
              buffer->contents()->at(buffer->line().line() + 1)->contents()))));
      buffer->contents()->erase(
          buffer->contents()->begin() + buffer->line().line() + 1);
      result->modified_buffer = true;
      needs_empty_line = true;
    }
    if (copy_to_paste_buffer_) {
      if (needs_empty_line) {
        deleted_text->AppendLine(editor_state, EmptyString());
      }
      InsertDeletedTextBuffer(editor_state, deleted_text);
    }
    result->undo = TransformationAtPosition(buffer->position(),
        NewInsertBufferTransformation(deleted_text, 1, START));
  }

  unique_ptr<Transformation> Clone() {
    return NewDeleteCharactersTransformation(
        repetitions_, copy_to_paste_buffer_);
  }

 private:
  size_t repetitions_;
  bool copy_to_paste_buffer_;
};

class DeleteWordTransformation : public Transformation {
 public:
  DeleteWordTransformation(StructureModifier structure_modifier,
                           bool copy_to_paste_buffer)
      : structure_modifier_(structure_modifier),
        copy_to_paste_buffer_(copy_to_paste_buffer) {}

  void Apply(
      EditorState* editor_state, OpenBuffer* buffer, Result* result) const {
    CHECK(buffer != nullptr);
    CHECK(result != nullptr);
    buffer->CheckPosition();
    buffer->MaybeAdjustPositionCol();

    LineColumn initial_position = buffer->position();
    LineColumn start, end;
    if (!buffer->BoundWordAt(initial_position, &start, &end)) {
      result->success = false;
      LOG(INFO) << "Unable to bound word, giving up.";
      return;
    }
    LOG(INFO) << "Starting at " << initial_position << " bound word from "
              << start << " to " << end;
    CHECK_EQ(start.line, end.line);
    CHECK_LE(start.column + 1, end.column);
    CHECK_LE(initial_position.line, start.line);

    shared_ptr<OpenBuffer> deleted_text(
        new OpenBuffer(editor_state, OpenBuffer::kPasteBuffer));
    unique_ptr<TransformationStack> stack(new TransformationStack);

    if (initial_position.line < start.line) {
      LOG(INFO) << "Deleting superfluous lines (from " << initial_position.line
                << " to " << start.line;
      while (initial_position.line < start.line) {
        start.line--;
        end.line--;
        stack->PushBack(
            NewDeleteLinesTransformation(1, FROM_CURRENT_POSITION_TO_END, true));
        stack->PushBack(NewDeleteCharactersTransformation(1, true));
      }
      start.column += initial_position.column;
      end.column += initial_position.column;
    }

    if (initial_position.column < start.column) {
      stack->PushBack(NewDeleteCharactersTransformation(
          start.column - initial_position.column, true));
      end.column = initial_position.column + end.column - start.column;
      start.column = initial_position.column;
    }

    CHECK_EQ(start.line, end.line);
    CHECK_EQ(start.line, initial_position.line);
    CHECK_LE(start.column, initial_position.column);
    CHECK_LT(initial_position.column, end.column);
    switch (structure_modifier_) {
      case ENTIRE_STRUCTURE:
        break;
      case FROM_BEGINNING_TO_CURRENT_POSITION:
        end = initial_position;
        break;
      case FROM_CURRENT_POSITION_TO_END:
        start = initial_position;
        break;
    }
    if (initial_position.column > start.column) {
      LOG(INFO) << "Scroll back: " << initial_position.column - start.column;
      stack->PushBack(NewSetRepetitionsTransformation(
          initial_position.column - start.column,
          NewDirectionTransformation(
              Direction::BACKWARDS,
              NewStructureTransformation(
                  CHAR, ENTIRE_STRUCTURE, NewMoveTransformation()))));
    }
    CHECK(end.column >= start.column);
    size_t size = end.column - start.column;
    LOG(INFO) << "Erasing word, characters: " << size;
    stack->PushBack(NewDeleteCharactersTransformation(size, true));
    stack->Apply(editor_state, buffer, result);
  }

  unique_ptr<Transformation> Clone() {
    return unique_ptr<Transformation>(new DeleteWordTransformation(
        structure_modifier_, copy_to_paste_buffer_));
  }

 private:
  StructureModifier structure_modifier_;
  bool copy_to_paste_buffer_;
};

class DeleteLinesTransformation : public Transformation {
 public:
  DeleteLinesTransformation(size_t repetitions,
                            StructureModifier structure_modifier,
                            bool copy_to_paste_buffer)
      : repetitions_(repetitions),
        structure_modifier_(structure_modifier),
        copy_to_paste_buffer_(copy_to_paste_buffer) {}

  void Apply(
      EditorState* editor_state, OpenBuffer* buffer, Result* result) const {
    BufferLineIterator line = buffer->line();
    size_t repetitions = min(repetitions_,
        buffer->contents()->size() - line.line());
    shared_ptr<OpenBuffer> deleted_text(
        new OpenBuffer(editor_state, OpenBuffer::kPasteBuffer));
    LOG(INFO) << "Erasing lines " << repetitions << " starting at line "
         << buffer->line().line() << " in a buffer with size "
         << buffer->contents()->size();

    for (size_t i = 0; i < repetitions; i++) {
      shared_ptr<LazyString> line_deletion;
      switch (structure_modifier_) {
        case ENTIRE_STRUCTURE:
          line_deletion = (*line)->contents();
          if ((*line)->activate() != nullptr) {
            (*line)->activate()->ProcessInput('d', editor_state);
          }
          break;

        case FROM_BEGINNING_TO_CURRENT_POSITION:
          line_deletion = (*line)->Substring(0, buffer->position().column);
          break;

        case FROM_CURRENT_POSITION_TO_END:
          line_deletion = (*line)->Substring(buffer->position().column);
          break;
      }
      deleted_text->AppendLine(editor_state, line_deletion);
      line++;
    }
    deleted_text->AppendLine(editor_state, EmptyString());
    if (copy_to_paste_buffer_) {
      InsertDeletedTextBuffer(editor_state, deleted_text);
    }

    LOG(INFO) << "Modifying buffer: " << structure_modifier_;
    if (structure_modifier_ == ENTIRE_STRUCTURE) {
      buffer->contents()->erase(
          buffer->contents()->begin() + buffer->line().line(),
          buffer->contents()->begin() + buffer->line().line() + repetitions);
      result->undo = TransformationAtPosition(
          LineColumn(buffer->position().line),
          ComposeTransformation(
              NewInsertBufferTransformation(deleted_text, 1, END),
              NewGotoPositionTransformation(buffer->position())));
      return;
    }
    unique_ptr<TransformationStack> stack(new TransformationStack);
    LineColumn position = buffer->position();
    for (size_t i = 0; i < repetitions; i++) {
      switch (structure_modifier_) {
        case ENTIRE_STRUCTURE:
          CHECK(false);

        case FROM_BEGINNING_TO_CURRENT_POSITION:
          stack->PushBack(NewGotoPositionTransformation(
              LineColumn(position.line + i)));
          stack->PushBack(
              NewDeleteCharactersTransformation(position.column, false));
          break;

        case FROM_CURRENT_POSITION_TO_END:
          stack->PushBack(NewGotoPositionTransformation(
              LineColumn(position.line + i, position.column)));
          stack->PushBack(
              NewDeleteCharactersTransformation(
                  buffer->LineAt(position.line + i)->size() - position.column,
                  false));
          break;
      }
    }
    stack->Apply(editor_state, buffer, result);
  }

  unique_ptr<Transformation> Clone() {
    return NewDeleteLinesTransformation(
        repetitions_, structure_modifier_, copy_to_paste_buffer_);
  }

 private:
  size_t repetitions_;
  StructureModifier structure_modifier_;
  bool copy_to_paste_buffer_;
};

class DeleteTransformation : public Transformation {
 public:
  DeleteTransformation(
      Structure structure,
      StructureModifier structure_modifier,
      size_t repetitions,
      bool copy_to_paste_buffer)
      : structure_(structure),
        structure_modifier_(structure_modifier),
        repetitions_(repetitions),
        copy_to_paste_buffer_(copy_to_paste_buffer) {}

  void Apply(
      EditorState* editor_state, OpenBuffer* buffer, Result* result) const {
    unique_ptr<Transformation> delegate;
    switch (structure_) {
      case CHAR:
        delegate = NewDeleteCharactersTransformation(
            repetitions_, copy_to_paste_buffer_);
        break;
      case WORD:
        delegate = NewDeleteWordsTransformation(
            repetitions_, structure_modifier_, copy_to_paste_buffer_);
        break;
      case LINE:
        delegate = NewDeleteLinesTransformation(
            repetitions_, structure_modifier_, copy_to_paste_buffer_);
        break;
      default:
        LOG(INFO) << "DeleteTransformation can't handle structure: "
                  << structure_;
        delegate = NewNoopTransformation();
    }
    return delegate->Apply(editor_state, buffer, result);
  }

  unique_ptr<Transformation> Clone() {
    return NewDeleteTransformation(
        structure_, structure_modifier_, repetitions_, copy_to_paste_buffer_);
  }

 private:
  Structure structure_;
  StructureModifier structure_modifier_;
  size_t repetitions_;
  bool copy_to_paste_buffer_;
};

}  // namespace

unique_ptr<Transformation> NewDeleteCharactersTransformation(
    size_t repetitions, bool copy_to_paste_buffer) {
  return unique_ptr<Transformation>(
      new DeleteCharactersTransformation(repetitions, copy_to_paste_buffer));
}

unique_ptr<Transformation> NewDeleteWordsTransformation(
    size_t repetitions, StructureModifier structure_modifier,
    bool copy_to_paste_buffer) {
  return NewApplyRepetitionsTransformation(repetitions,
      unique_ptr<Transformation>(new DeleteWordTransformation(
          structure_modifier, copy_to_paste_buffer)));
}

unique_ptr<Transformation> NewDeleteLinesTransformation(
    size_t repetitions, StructureModifier structure_modifier,
    bool copy_to_paste_buffer) {
  return unique_ptr<Transformation>(new DeleteLinesTransformation(
      repetitions, structure_modifier, copy_to_paste_buffer));
}

unique_ptr<Transformation> NewDeleteTransformation(
    Structure structure, StructureModifier structure_modifier,
    size_t repetitions, bool copy_to_paste_buffer) {
  return unique_ptr<Transformation>(new DeleteTransformation(
      structure, structure_modifier, repetitions, copy_to_paste_buffer));
}

}  // namespace editor
}  // namespace afc