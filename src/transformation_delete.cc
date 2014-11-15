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
    LOG(INFO) << "Starting DeleteCharactersTransformation.";
    if (buffer->contents()->empty()) {
      result->success = false;
      return;
    }
    buffer->CheckPosition();
    buffer->set_position(min(buffer->position(), buffer->end_position()));
    size_t current_line = buffer->line().line();
    buffer->MaybeAdjustPositionCol();

    shared_ptr<LazyString> preserved_contents =
        StartOfLine(buffer, current_line, buffer->current_position_col());

    size_t line;
    size_t chars_erased;
    SkipLinesToErase(buffer, preserved_contents, &line, &chars_erased);
    LOG(INFO) << "Erasing from line " << current_line << " to line " << line
              << " would erase " << chars_erased << " characters.";

    // The amount of characters that should be erased from the current line.
    // Depending on the direction, we'll erase from the beginning (FORWARDS) or
    // the end of the current line (BACKWARDS).  If the line is the current
    // line, this already includes characters in preserved_contents.
    size_t chars_erase_line = buffer->LineAt(line)->size()
        + (direction_ == FORWARDS ? 1 : 0)
        - min(buffer->LineAt(line)->size(),
              (repetitions_ < chars_erased ? chars_erased - repetitions_ : 0));
    if (chars_erase_line > buffer->LineAt(line)->size()) {
      LOG(INFO) << "Adjusting for end of buffer.";
      CHECK_EQ(chars_erase_line, buffer->LineAt(line)->size() + 1);
      chars_erase_line = 0;
      if (!AdvanceLine(buffer, &line)) {
        chars_erase_line = buffer->LineAt(line)->size();
      }
    }
    LOG(INFO) << "Characters to erase from current line: " << chars_erase_line
              << ", repetitions: " << repetitions_ << ", chars_erased: "
              << chars_erased << ", preserved_contents size: "
              << preserved_contents->size() << ", actual length: "
              << buffer->LineAt(line)->size();

    result->success = chars_erased >= repetitions_;
    result->made_progress = chars_erased + chars_erase_line > 0;

    size_t line_begin = min(line, current_line);
    size_t line_end = max(line, current_line);

    shared_ptr<OpenBuffer> delete_buffer = GetDeletedTextBuffer(
        editor_state, buffer, line_begin, line_end, preserved_contents,
        chars_erase_line);
    if (copy_to_paste_buffer_) {
      result->delete_buffer->Apply(
          editor_state, NewInsertBufferTransformation(delete_buffer, 1, END));
    }

    if (direction_ == BACKWARDS) {
      buffer->set_position(
          LineColumn(line, buffer->LineAt(line)->size() - chars_erase_line));
    }

    LOG(INFO) << "Storing new line (at position " << max(current_line, line)
              << ").";
    buffer->contents()->at(line_end).reset(new Line(Line::Options(
        ProduceFinalLine(buffer, preserved_contents, line, chars_erase_line))));

    LOG(INFO) << "Erasing lines in range [" << line_begin << ", " << line_end
              << ").";
    buffer->contents()->erase(
        buffer->contents()->begin() + line_begin,
        buffer->contents()->begin() + line_end);
    result->modified_buffer = true;

    result->undo = TransformationAtPosition(buffer->position(),
        NewInsertBufferTransformation(
            delete_buffer, 1, direction_ == FORWARDS ? START : END));
  }

  unique_ptr<Transformation> Clone() {
    return NewDeleteCharactersTransformation(
        direction_, repetitions_, copy_to_paste_buffer_);
  }

 private:
  shared_ptr<OpenBuffer> GetDeletedTextBuffer(
      EditorState* editor_state, OpenBuffer* buffer, size_t line_begin,
      size_t line_end, const shared_ptr<LazyString>& preserved_contents,
      size_t chars_erase_line) const {
    LOG(INFO) << "Preparing deleted text buffer.";
    shared_ptr<OpenBuffer> delete_buffer(
        new OpenBuffer(editor_state, OpenBuffer::kPasteBuffer));

    if (line_begin == line_end) {
      auto end_line = buffer->LineAt(line_begin);
      size_t start = direction_ == FORWARDS
          ? preserved_contents->size()
          : end_line->size() - chars_erase_line;
      CHECK_LE(start, end_line->size());
      size_t end = direction_ == FORWARDS
          ? chars_erase_line
          : end_line->size() - preserved_contents->size();
      CHECK_LE(start, end);
      CHECK_LE(end, end_line->size());
      LOG(INFO) << "Preserving chars from single line: [" << start << ", "
                << end << "): "
                << end_line->Substring(start, end - start)->ToString();
      delete_buffer
          ->AppendLine(editor_state, end_line->Substring(start, end - start));
      return delete_buffer;
    }

    delete_buffer->AppendLine(editor_state,
        buffer->LineAt(line_begin)->Substring(
            direction_ == FORWARDS
            ? preserved_contents->size()
            : buffer->LineAt(line_begin)->size() - chars_erase_line));

    for (size_t i = line_begin + 1; i < line_end; i++) {
      delete_buffer->AppendLine(editor_state, buffer->LineAt(i)->contents());
    }

    delete_buffer->AppendLine(editor_state,
        buffer->LineAt(line_end)->Substring(0,
            direction_ == FORWARDS
                ? chars_erase_line
                : buffer->LineAt(line_end)->size() - preserved_contents->size()));
    return delete_buffer;
  }

  shared_ptr<LazyString> StartOfLine(
      OpenBuffer* buffer, size_t line_number, size_t column,
      Direction direction) const {
    auto line = buffer->LineAt(line_number);
    switch (direction) {
      case FORWARDS:
        return line->Substring(0, column);
      case BACKWARDS:
        return line->Substring(column);
      default:
        CHECK(false);
    }
  }

  shared_ptr<LazyString> StartOfLine(
      OpenBuffer* buffer, size_t line_number, size_t column) const {
    return StartOfLine(buffer, line_number, column, direction_);
  }

  shared_ptr<LazyString> EndOfLine(
      OpenBuffer* buffer, size_t line_number, size_t chars_to_erase) const {
    return StartOfLine(
        buffer, line_number, chars_to_erase, ReverseDirection(direction_));
  }

  shared_ptr<LazyString> ProduceFinalLine(
      OpenBuffer* buffer, const shared_ptr<LazyString> preserved_contents,
      size_t line, size_t chars_to_erase) const {
    switch (direction_) {
      case FORWARDS:
        return StringAppend(preserved_contents,
             EndOfLine(buffer, line, chars_to_erase));
      case BACKWARDS:
        auto end_line = buffer->LineAt(line);
        return StringAppend(
             StartOfLine(buffer, line, end_line->size() - chars_to_erase,
                         FORWARDS),
             preserved_contents);
    }
    CHECK(false);
  }

  // Loop away from the current line (in the direction given by direction_),
  // stopping at the first line such that if we erase all characters in it
  // (including \n), we will have erased at least as many characters as needed.
  //
  // chars_erased will be set to the total number of characters erased from the
  // current position until (including) line.
  void SkipLinesToErase(const OpenBuffer* buffer,
                        const shared_ptr<LazyString>& preserved_contents,
                        size_t* line, size_t* chars_erased) const {
    size_t current_line = buffer->current_position_line();
    *line = current_line;
    *chars_erased = 0;
    if (direction_ == FORWARDS && *line == buffer->contents()->size()) {
      return;
    }

    while (true) {
      CHECK_LT(*line, buffer->contents()->size());
      LOG(INFO) << "Iteration at line " << *line << " having already erased "
                << *chars_erased << " characters.";
      size_t chars_in_line = buffer->LineAt(*line)->size();
      if (*line == current_line) {
        CHECK_GE(chars_in_line, preserved_contents->size());
        chars_in_line -= preserved_contents->size();
        if (direction_ == FORWARDS) {
          chars_in_line++;
        }
      } else if (*line + 1 < buffer->contents()->size()) {
        chars_in_line++;  // The new line character.
      }
      LOG(INFO) << "Characters available in line: " << chars_in_line;
      *chars_erased += chars_in_line;
      if (*chars_erased >= repetitions_) {
        return;
      }
      CHECK_LT(*chars_erased, repetitions_);

      if (!AdvanceLine(buffer, line)) {
        return;
      }
    }
  }

  bool AdvanceLine(const OpenBuffer* buffer, size_t* line) const {
    size_t old_value = *line;
    switch (direction_) {
      case FORWARDS:
        if (*line + 1 < buffer->contents()->size()) { (*line)++; }
        break;
      case BACKWARDS:
        if (*line > 0) { (*line)--; }
        break;
    }
    CHECK_LT(*line, buffer->contents()->size());
    return old_value != *line;
  }

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

    unique_ptr<TransformationStack> stack(new TransformationStack);

    if (initial_position.line < start.line) {
      LOG(INFO) << "Deleting superfluous lines (from " << initial_position.line
                << " to " << start.line;
      while (initial_position.line < start.line) {
        start.line--;
        end.line--;
        stack->PushBack(
            NewDeleteLinesTransformation(
                1, FROM_CURRENT_POSITION_TO_END, Modifiers(), true));
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
                            const Modifiers& modifiers,
                            bool copy_to_paste_buffer)
      : repetitions_(repetitions),
        structure_modifier_(structure_modifier),
        modifiers_(modifiers),
        copy_to_paste_buffer_(copy_to_paste_buffer) {}

  void Apply(
      EditorState* editor_state, OpenBuffer* buffer, Result* result) const {
    BufferLineIterator line = buffer->line();
    size_t repetitions = min(repetitions_,
        buffer->contents()->size() - line.line());
    if (modifiers_.strength == Modifiers::WEAK) {
      repetitions = 1;  // We won't be able to cross boundaries.
    }
    shared_ptr<OpenBuffer> delete_buffer(
        new OpenBuffer(editor_state, OpenBuffer::kPasteBuffer));
    LOG(INFO) << "Erasing lines " << repetitions << " starting at line "
         << buffer->line().line() << " in a buffer with size "
         << buffer->contents()->size() << " with modifiers: " << modifiers_;

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
      delete_buffer->AppendLine(editor_state, line_deletion);
      line++;
    }
    if (modifiers_.strength != Modifiers::WEAK) {
      delete_buffer->AppendLine(editor_state, EmptyString());
    }
    if (copy_to_paste_buffer_) {
      result->delete_buffer->Apply(
          editor_state, NewInsertBufferTransformation(delete_buffer, 1, END));
    }

    LOG(INFO) << "Modifying buffer: " << structure_modifier_;
    if (structure_modifier_ == ENTIRE_STRUCTURE
        && modifiers_.strength != Modifiers::WEAK) {
      buffer->contents()->erase(
          buffer->contents()->begin() + buffer->line().line(),
          buffer->contents()->begin() + buffer->line().line() + repetitions);
      result->modified_buffer = true;
      result->undo = TransformationAtPosition(
          LineColumn(buffer->position().line),
          ComposeTransformation(
              NewInsertBufferTransformation(delete_buffer, 1, END),
              NewGotoPositionTransformation(buffer->position())));
      return;
    }
    unique_ptr<TransformationStack> stack(new TransformationStack);
    LineColumn position = buffer->position();
    for (size_t i = 0; i < repetitions; i++) {
      switch (structure_modifier_) {
        case ENTIRE_STRUCTURE:
          CHECK(modifiers_.strength == Modifiers::WEAK);
          stack->PushBack(NewGotoPositionTransformation(
              LineColumn(position.line + i)));
          stack->PushBack(NewDeleteCharactersTransformation(
              FORWARDS, buffer->LineAt(position.line + i)->size(), false));
          break;

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
        repetitions_, structure_modifier_, modifiers_, copy_to_paste_buffer_);
  }

 private:
  size_t repetitions_;
  StructureModifier structure_modifier_;
  Modifiers modifiers_;
  bool copy_to_paste_buffer_;
};

class DeleteBufferTransformation : public Transformation {
 public:
  DeleteBufferTransformation(StructureModifier structure_modifier,
                             bool copy_to_paste_buffer)
      : structure_modifier_(structure_modifier),
        copy_to_paste_buffer_(copy_to_paste_buffer) {}

  void Apply(
      EditorState* editor_state, OpenBuffer* buffer, Result* result) const {
    LOG(INFO) << "Erasing buffer (modifier: " << structure_modifier_
              << ") of size: " << buffer->contents()->size();

    int current_line = buffer->line().line();
    int last_line = buffer->contents()->size();

    int begin = 0;
    int end = last_line;
    switch (structure_modifier_) {
      case ENTIRE_STRUCTURE:
        break;  // We're all set.
      case FROM_BEGINNING_TO_CURRENT_POSITION:
        end = current_line;
        break;
      case FROM_CURRENT_POSITION_TO_END:
        begin = current_line;
        break;
    }

    CHECK_LE(begin, end);
    // TODO(alejo): Handle reverse?
    TransformationAtPosition(LineColumn(begin),
        NewDeleteLinesTransformation(
            end - begin, ENTIRE_STRUCTURE, Modifiers(), copy_to_paste_buffer_))
        ->Apply(editor_state, buffer, result);
  }

  unique_ptr<Transformation> Clone() {
    return NewDeleteBufferTransformation(
        structure_modifier_, copy_to_paste_buffer_);
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
      const Modifiers& modifiers,
      bool copy_to_paste_buffer)
      : structure_(structure),
        structure_modifier_(structure_modifier),
        direction_(direction),
        repetitions_(repetitions),
        modifiers_(modifiers),
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
            repetitions_, structure_modifier_, modifiers_,
            copy_to_paste_buffer_);
        break;
      case BUFFER:
        delegate = NewDeleteBufferTransformation(
            structure_modifier_, copy_to_paste_buffer_);
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
        Modifiers(), copy_to_paste_buffer_);
  }

 private:
  Structure structure_;
  StructureModifier structure_modifier_;
  Direction direction_;
  size_t repetitions_;
  Modifiers modifiers_;
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
    const Modifiers& modifiers, bool copy_to_paste_buffer) {
  return unique_ptr<Transformation>(new DeleteLinesTransformation(
      repetitions, structure_modifier, modifiers, copy_to_paste_buffer));
}

unique_ptr<Transformation> NewDeleteBufferTransformation(
    StructureModifier structure_modifier,
    bool copy_to_paste_buffer) {
  return unique_ptr<Transformation>(new DeleteBufferTransformation(
      structure_modifier, copy_to_paste_buffer));
}

unique_ptr<Transformation> NewDeleteTransformation(
    Structure structure, StructureModifier structure_modifier,
    Direction direction, size_t repetitions, const Modifiers& modifiers,
    bool copy_to_paste_buffer) {
  return unique_ptr<Transformation>(new DeleteTransformation(
      structure, structure_modifier, direction, repetitions, modifiers,
      copy_to_paste_buffer));
}

}  // namespace editor
}  // namespace afc
