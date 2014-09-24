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
  DeleteCharactersTransformation(
      Direction direction, size_t repetitions, bool copy_to_paste_buffer)
      : direction_(direction),
        repetitions_(repetitions),
        copy_to_paste_buffer_(copy_to_paste_buffer) {}

  void Apply(
      EditorState* editor_state, OpenBuffer* buffer, Result* result) const {
    buffer->CheckPosition();
    buffer->MaybeAdjustPositionCol();
    shared_ptr<OpenBuffer> deleted_text(
        new OpenBuffer(editor_state, OpenBuffer::kPasteBuffer));
    shared_ptr<LazyString> preserved_contents;
    size_t current_line = buffer->line().line();
    switch (direction_) {
      case FORWARDS:
        preserved_contents =
            (*buffer->line())->Substring(0, buffer->current_position_col());
        break;
      case BACKWARDS:
        preserved_contents =
            (*buffer->line())->Substring(buffer->current_position_col());
        break;
    }

    size_t line = current_line;
    size_t chars_erased = 0;
    bool reached_end = direction_ == FORWARDS
        && line == buffer->contents()->size();

    // Loop until we've found the last line such that if we erase all
    // characters in it (including \n), we still have not erased more characters
    // than needed.
    while (!reached_end) {
      LOG(INFO) << "Iteration at line " << line << " having already erased "
                << chars_erased << " characters.";
      size_t chars_in_line = buffer->LineAt(line)->size();
      if (direction_ == FORWARDS
          ? line + 1 < buffer->contents()->size() : line > 0 ) {
        chars_in_line++;  // The new line character.
      }
      if (line == current_line) {
        CHECK_GE(chars_in_line, preserved_contents->size());
        chars_in_line -= preserved_contents->size();
      }
      LOG(INFO) << "Characters available in line: " << chars_in_line;
      if (chars_erased + chars_in_line > repetitions_) {
        break;
      }
      chars_erased += chars_in_line;

      switch (direction_) {
        case FORWARDS:
          if (line + 1 == buffer->contents()->size()) {
            reached_end = true;
          } else {
            line++;
          }
          break;
        case BACKWARDS:
          if (line == 0) {
            reached_end = true;
          } else {
            line--;
          }
          break;
      }
    }

    // The amount of characters that should be erased from the current line.
    // Depending on the direction, we'll erase from the beginning (FORWARDS) or
    // the end of the current line (BACKWARDS).
    CHECK_GE(repetitions_, chars_erased);
    size_t chars_erase_line = min(buffer->LineAt(line)->size(),
        repetitions_ - chars_erased
        + (line == current_line ? preserved_contents->size() : 0));
    LOG(INFO) << "Characters to erase from current line: " << chars_erase_line;

    result->success = chars_erased + chars_erase_line >= repetitions_;
    result->made_progress = chars_erased + chars_erase_line > 0;

    shared_ptr<LazyString> line_contents;
    if (false) {
      LOG(INFO) << "Reached end.";
      chars_erase_line = buffer->LineAt(line)->size();
      line_contents = preserved_contents;
      if (direction_ == BACKWARDS) {
        buffer->set_position(LineColumn(0));
      }
    } else {
      LOG(INFO) << "Didn't reach end (chars erased: " << chars_erased
                << ", repetitions: " << repetitions_ << ")";
      switch (direction_) {
        case FORWARDS:
          {
            auto end_line = buffer->LineAt(line);
            line_contents = StringAppend(preserved_contents,
                end_line->Substring(chars_erase_line));
            if (line != current_line) {
              deleted_text->AppendLine(editor_state,
                  buffer->LineAt(current_line)
                      ->Substring(preserved_contents->size()));
            }
          }
          break;
        case BACKWARDS:
          {
            auto end_line = buffer->LineAt(line);
            line_contents = StringAppend(
                end_line->Substring(0, end_line->size() - chars_erase_line),
                preserved_contents);
            if (line != current_line) {
              deleted_text->AppendLine(editor_state,
                  end_line->Substring(end_line->size() - chars_erase_line));
            }
            buffer->set_position(
                LineColumn(line, end_line->size() - chars_erase_line));
          }
          break;
      }
    }

    LOG(INFO) << "Preparing deleted text buffer.";
    if (line == current_line) {
      auto end_line = buffer->LineAt(line);
      size_t start = direction_ == FORWARDS
          ? preserved_contents->size() : end_line->size() - chars_erase_line;
      CHECK_LE(start, end_line->size());
      deleted_text->AppendLine(editor_state, end_line->Substring(
          start, min(repetitions_, end_line->size() - start)));
    } else {
      for (size_t i = min(line, current_line) + 1; i < max(line, current_line);
           i++) {
        deleted_text->AppendLine(editor_state, buffer->LineAt(i)->contents());
      }
      LOG(INFO) << "And preparing last line for deleted text buffer.";
      auto last_line = buffer->LineAt(max(line, current_line));
      deleted_text->AppendLine(editor_state, last_line->Substring(0,
          direction_ == FORWARDS
              ? chars_erase_line : preserved_contents->size()));
    }

    LOG(INFO) << "Storing new line (at position " << max(current_line, line)
              << ").";
    buffer->contents()->at(max(current_line, line)).reset(
        new Line(Line::Options(line_contents)));

    LOG(INFO) << "Erasing lines in range [" << min(current_line, line) << ", "
              << max(current_line, line) << ").";
    buffer->contents()->erase(
        buffer->contents()->begin() + min(current_line, line),
        buffer->contents()->begin() + max(current_line, line));
    result->modified_buffer = true;


    if (copy_to_paste_buffer_) {
      InsertDeletedTextBuffer(editor_state, deleted_text);
    }
    result->undo = TransformationAtPosition(buffer->position(),
        NewInsertBufferTransformation(deleted_text, 1, START));
  }

  unique_ptr<Transformation> Clone() {
    return NewDeleteCharactersTransformation(
        direction_, repetitions_, copy_to_paste_buffer_);
  }

 private:
  Direction direction_;
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
        stack->PushBack(NewDeleteCharactersTransformation(FORWARDS, 1, true));
      }
      start.column += initial_position.column;
      end.column += initial_position.column;
    }

    if (initial_position.column < start.column) {
      stack->PushBack(NewDeleteCharactersTransformation(
          FORWARDS, start.column - initial_position.column, true));
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
    stack->PushBack(NewDeleteCharactersTransformation(FORWARDS, size, true));
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
          stack->PushBack(NewDeleteCharactersTransformation(
              FORWARDS, position.column, false));
          break;

        case FROM_CURRENT_POSITION_TO_END:
          stack->PushBack(NewGotoPositionTransformation(
              LineColumn(position.line + i, position.column)));
          stack->PushBack(NewDeleteCharactersTransformation(FORWARDS,
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
      Direction direction,
      size_t repetitions,
      bool copy_to_paste_buffer)
      : structure_(structure),
        structure_modifier_(structure_modifier),
        direction_(direction),
        repetitions_(repetitions),
        copy_to_paste_buffer_(copy_to_paste_buffer) {}

  void Apply(
      EditorState* editor_state, OpenBuffer* buffer, Result* result) const {
    unique_ptr<Transformation> delegate;
    switch (structure_) {
      case CHAR:
        delegate = NewDeleteCharactersTransformation(
            direction_, repetitions_, copy_to_paste_buffer_);
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
        structure_, structure_modifier_, direction_, repetitions_,
        copy_to_paste_buffer_);
  }

 private:
  Structure structure_;
  StructureModifier structure_modifier_;
  Direction direction_;
  size_t repetitions_;
  bool copy_to_paste_buffer_;
};

}  // namespace

unique_ptr<Transformation> NewDeleteCharactersTransformation(
    Direction direction, size_t repetitions, bool copy_to_paste_buffer) {
  return unique_ptr<Transformation>(new DeleteCharactersTransformation(
      direction, repetitions, copy_to_paste_buffer));
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
    Direction direction, size_t repetitions, bool copy_to_paste_buffer) {
  return unique_ptr<Transformation>(new DeleteTransformation(
      structure, structure_modifier, direction, repetitions,
      copy_to_paste_buffer));
}

}  // namespace editor
}  // namespace afc
