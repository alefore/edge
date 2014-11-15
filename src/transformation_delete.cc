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
      const Modifiers& modifiers, bool copy_to_paste_buffer)
      : modifiers_(modifiers),
        copy_to_paste_buffer_(copy_to_paste_buffer) {}

  void Apply(
      EditorState* editor_state, OpenBuffer* buffer, Result* result) const {
    LOG(INFO) << "Starting DeleteCharactersTransformation.";
    if (buffer->contents()->empty()) {
      result->success = false;
      return;
    }
    if (modifiers_.repetitions == 0) {
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
        + (modifiers_.direction == FORWARDS ? 1 : 0)
        - min(buffer->LineAt(line)->size(),
              (modifiers_.repetitions < chars_erased
                   ? chars_erased - modifiers_.repetitions
                   : 0));
    if (chars_erase_line > buffer->LineAt(line)->size()) {
      LOG(INFO) << "Adjusting for end of buffer.";
      CHECK_EQ(chars_erase_line, buffer->LineAt(line)->size() + 1);
      chars_erase_line = 0;
      if (!AdvanceLine(buffer, &line)) {
        chars_erase_line = buffer->LineAt(line)->size();
      }
    }
    LOG(INFO) << "Characters to erase from current line: " << chars_erase_line
              << ", modifiers: " << modifiers_ << ", chars_erased: "
              << chars_erased << ", preserved_contents size: "
              << preserved_contents->size() << ", actual length: "
              << buffer->LineAt(line)->size();

    result->success = chars_erased >= modifiers_.repetitions;
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

    if (modifiers_.direction == BACKWARDS) {
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
            delete_buffer, 1, modifiers_.direction == FORWARDS ? START : END));
  }

  unique_ptr<Transformation> Clone() {
    return NewDeleteCharactersTransformation(
        modifiers_, copy_to_paste_buffer_);
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
      size_t start = modifiers_.direction == FORWARDS
          ? preserved_contents->size()
          : end_line->size() - chars_erase_line;
      CHECK_LE(start, end_line->size());
      size_t end = modifiers_.direction == FORWARDS
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
            modifiers_.direction == FORWARDS
            ? preserved_contents->size()
            : buffer->LineAt(line_begin)->size() - chars_erase_line));

    for (size_t i = line_begin + 1; i < line_end; i++) {
      delete_buffer->AppendLine(editor_state, buffer->LineAt(i)->contents());
    }

    delete_buffer->AppendLine(editor_state,
        buffer->LineAt(line_end)->Substring(0,
            modifiers_.direction == FORWARDS
                ? chars_erase_line
                : buffer->LineAt(line_end)->size() - preserved_contents->size()));
    return delete_buffer;
  }

  static shared_ptr<LazyString> StartOfLine(
      OpenBuffer* buffer, size_t line_number, size_t column,
      Direction direction) {
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
    return StartOfLine(buffer, line_number, column, modifiers_.direction);
  }

  shared_ptr<LazyString> EndOfLine(
      OpenBuffer* buffer, size_t line_number, size_t chars_to_erase) const {
    return StartOfLine(buffer, line_number, chars_to_erase,
                       ReverseDirection(modifiers_.direction));
  }

  shared_ptr<LazyString> ProduceFinalLine(
      OpenBuffer* buffer, const shared_ptr<LazyString> preserved_contents,
      size_t line, size_t chars_to_erase) const {
    switch (modifiers_.direction) {
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

  // Loop away from the current line (in the direction given), stopping at the
  // first line such that if we erase all characters in it (including \n), we
  // will have erased at least as many characters as needed.
  //
  // chars_erased will be set to the total number of characters erased from the
  // current position until (including) line.
  void SkipLinesToErase(const OpenBuffer* buffer,
                        const shared_ptr<LazyString>& preserved_contents,
                        size_t* line, size_t* chars_erased) const {
    size_t current_line = buffer->current_position_line();
    *line = current_line;
    *chars_erased = 0;
    if (modifiers_.direction == FORWARDS
        && *line == buffer->contents()->size()) {
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
        if (modifiers_.direction == FORWARDS) {
          chars_in_line++;
        }
      } else if (*line + 1 < buffer->contents()->size()) {
        chars_in_line++;  // The new line character.
      }
      LOG(INFO) << "Characters available in line: " << chars_in_line;
      *chars_erased += chars_in_line;
      if (*chars_erased >= modifiers_.repetitions) {
        return;
      }
      CHECK_LT(*chars_erased, modifiers_.repetitions);

      if (!AdvanceLine(buffer, line)) {
        return;
      }
    }
  }

  bool AdvanceLine(const OpenBuffer* buffer, size_t* line) const {
    size_t old_value = *line;
    switch (modifiers_.direction) {
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

  Modifiers modifiers_;
  bool copy_to_paste_buffer_;
};

class DeleteWordTransformation : public Transformation {
 public:
  DeleteWordTransformation(const Modifiers& modifiers,
                           bool copy_to_paste_buffer)
      : modifiers_(modifiers),
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
        Modifiers modifiers;
        modifiers.structure_range = Modifiers::FROM_CURRENT_POSITION_TO_END;
        stack->PushBack(NewDeleteLinesTransformation(modifiers, true));
        stack->PushBack(NewDeleteCharactersTransformation(Modifiers(), true));
      }
      start.column += initial_position.column;
      end.column += initial_position.column;
    }

    if (initial_position.column < start.column) {
      Modifiers modifiers;
      modifiers.repetitions = start.column - initial_position.column;
      stack->PushBack(NewDeleteCharactersTransformation(modifiers, true));
      end.column = initial_position.column + end.column - start.column;
      start.column = initial_position.column;
    }

    CHECK_EQ(start.line, end.line);
    CHECK_EQ(start.line, initial_position.line);
    CHECK_LE(start.column, initial_position.column);
    CHECK_LT(initial_position.column, end.column);
    switch (modifiers_.structure_range) {
      case Modifiers::ENTIRE_STRUCTURE:
        break;
      case Modifiers::FROM_BEGINNING_TO_CURRENT_POSITION:
        end = initial_position;
        break;
      case Modifiers::FROM_CURRENT_POSITION_TO_END:
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
                  CHAR, Modifiers::ENTIRE_STRUCTURE,
                  NewMoveTransformation()))));
    }
    CHECK(end.column >= start.column);
    size_t size = end.column - start.column;
    LOG(INFO) << "Erasing word, characters: " << size;
    Modifiers modifiers;
    modifiers.repetitions = size;
    stack->PushBack(NewDeleteCharactersTransformation(modifiers, true));
    stack->Apply(editor_state, buffer, result);
  }

  unique_ptr<Transformation> Clone() {
    return NewDeleteWordsTransformation(modifiers_, copy_to_paste_buffer_);
  }

 private:
  Modifiers modifiers_;
  bool copy_to_paste_buffer_;
};

class DeleteLinesTransformation : public Transformation {
 public:
  DeleteLinesTransformation(const Modifiers& modifiers,
                            bool copy_to_paste_buffer)
      : modifiers_(modifiers),
        copy_to_paste_buffer_(copy_to_paste_buffer) {}

  void Apply(
      EditorState* editor_state, OpenBuffer* buffer, Result* result) const {
    BufferLineIterator line = buffer->line();
    size_t repetitions = min(modifiers_.repetitions,
        buffer->contents()->size() - line.line());
    shared_ptr<OpenBuffer> delete_buffer(
        new OpenBuffer(editor_state, OpenBuffer::kPasteBuffer));
    LOG(INFO) << "Erasing lines " << repetitions << " starting at line "
         << buffer->line().line() << " in a buffer with size "
         << buffer->contents()->size() << " with modifiers: " << modifiers_;

    bool forwards = modifiers_.structure_range
        != Modifiers::FROM_BEGINNING_TO_CURRENT_POSITION;
    bool backwards = modifiers_.structure_range
        != Modifiers::FROM_CURRENT_POSITION_TO_END;

    for (size_t i = 0; i < repetitions; i++) {
      shared_ptr<LazyString> line_deletion;
      size_t start = backwards ? 0 : buffer->position().column;
      size_t end = forwards ? (*line)->size() : buffer->position().column;
      if (start == 0
          && end == (*line)->size()
          && (*line)->activate() != nullptr) {
        (*line)->activate()->ProcessInput('d', editor_state);
      }
      line_deletion = (*line)->Substring(start, end - start);
      delete_buffer->AppendLine(editor_state, line_deletion);
      line++;
    }
    if (forwards && modifiers_.strength > Modifiers::WEAK) {
      delete_buffer->AppendLine(editor_state, EmptyString());
    }
    if (copy_to_paste_buffer_) {
      result->delete_buffer->Apply(
          editor_state, NewInsertBufferTransformation(delete_buffer, 1, END));
    }

    LOG(INFO) << "Modifying buffer: " << modifiers_.structure_range;
    if (modifiers_.structure_range == Modifiers::ENTIRE_STRUCTURE
        && modifiers_.strength == Modifiers::DEFAULT) {
      // Optimization.
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
      auto line = buffer->LineAt(position.line + i);
      // The column in the current line that we should move to before deleting.
      size_t column = backwards
          ? min(min(position.column, line->size()),
                FindStartOfLine(buffer, line.get()))
          : position.column;
      size_t delete_characters = forwards
          ? max(column, FindLengthOfLine(buffer, line.get())) - column
          : position.column - column;
      LineColumn delete_from(position.line + i, column);
      LOG(INFO) << "Deleting from " << delete_from << " characters: "
                << delete_characters;
      if (repetitions > 1) {
        delete_characters++;  // The newline character.
      }
      Modifiers modifiers;
      modifiers.repetitions = delete_characters;
      stack->PushBack(NewGotoPositionTransformation(delete_from));
      stack->PushBack(NewDeleteCharactersTransformation(modifiers, false));
    }
    size_t delete_characters_before = 0;
    size_t delete_characters_after = 0;
    switch (modifiers_.strength) {
      case Modifiers::VERY_WEAK:
      case Modifiers::WEAK:
        break;
      case Modifiers::DEFAULT:
        if (forwards) {
          delete_characters_after = 1;
        }
        break;
      case Modifiers::STRONG:
        if (forwards) {
          delete_characters_after = 1;
        }
        if (backwards && position.line > 0) {
          delete_characters_before = 1;
        }
        break;
      case Modifiers::VERY_STRONG:
        if (forwards) {
          size_t position_line = position.line;
          while (position_line + 1 < buffer->contents()->size()) {
            delete_characters_after++;
            auto line = buffer->LineAt(position_line + 1);
            size_t start = FindSoftStartOfLine(buffer, line.get());
            VLOG(5) << "Deleting forwards at line " << position_line
                    << " consumes up to character " << start << " of "
                    << line->size();
            CHECK_LE(start, line->size());
            delete_characters_after += start;
            if (start < line->size()) { break; }
            position_line++;
          }
        }
        if (backwards) {
          size_t position_line = position.line;
          while (position_line > 0) {
            delete_characters_before++;  // The newline.
            auto line = buffer->LineAt(position_line - 1);
            size_t length = FindSoftLengthOfLine(buffer, line.get());
            VLOG(5) << "Deleting backwards at line " << position_line
                    << " consumes down to character " << length << " of "
                    << line->size();
            CHECK_LE(length, line->size());
            delete_characters_before += line->size() - length;
            if (length > 0) { break; }
            position_line--;
          }
        }
        break;
    }
    if (delete_characters_after > 0) {
      LOG(INFO) << "Deleting characters after: " << delete_characters_after;
      stack->PushBack(NewGotoPositionTransformation(
          LineColumn(position.line, buffer->LineAt(position.line)->size())));
      Modifiers modifiers;
      modifiers.repetitions = delete_characters_after;
      stack->PushBack(NewDeleteCharactersTransformation(modifiers, false));
    }
    if (delete_characters_before > 0) {
      LOG(INFO) << "Deleting characters before: " << delete_characters_before;
      stack->PushBack(NewGotoPositionTransformation(LineColumn(position.line)));
      Modifiers modifiers;
      modifiers.direction = BACKWARDS;
      modifiers.repetitions = delete_characters_before;
      stack->PushBack(NewDeleteCharactersTransformation(modifiers, false));
    }
    stack->Apply(editor_state, buffer, result);
  }

  unique_ptr<Transformation> Clone() {
    return NewDeleteLinesTransformation(modifiers_, copy_to_paste_buffer_);
  }

 private:
  size_t FindStartOfLine(OpenBuffer* buffer, const Line* line) const {
    if (modifiers_.strength == Modifiers::VERY_WEAK) {
      return FindSoftStartOfLine(buffer, line);
    }
    return 0;
  }
  static size_t FindSoftStartOfLine(OpenBuffer* buffer, const Line* line) {
    const string& word_chars =
        buffer->read_string_variable(buffer->variable_word_characters());
    size_t start = 0;
    while (start < line->size()
           && word_chars.find(line->get(start)) == string::npos) {
      start++;
    }
    return start;
  }

  size_t FindLengthOfLine(OpenBuffer* buffer, const Line* line) const {
    if (modifiers_.strength == Modifiers::VERY_WEAK) {
      return FindSoftLengthOfLine(buffer, line);
    }
    return line->size();
  }

  static size_t FindSoftLengthOfLine(OpenBuffer* buffer, const Line* line) {
    const string& word_chars =
        buffer->read_string_variable(buffer->variable_word_characters());
    size_t length = line->size();
    while (length > 0
           && word_chars.find(line->get(length - 1)) == string::npos) {
      length--;
    }
    return length;
  }

  Modifiers modifiers_;
  bool copy_to_paste_buffer_;
};

class DeleteBufferTransformation : public Transformation {
 public:
  DeleteBufferTransformation(const Modifiers& modifiers,
                             bool copy_to_paste_buffer)
      : modifiers_(modifiers),
        copy_to_paste_buffer_(copy_to_paste_buffer) {}

  void Apply(
      EditorState* editor_state, OpenBuffer* buffer, Result* result) const {
    LOG(INFO) << "Erasing buffer (modifiers: " << modifiers_ << ") of size: "
              << buffer->contents()->size();

    int current_line = buffer->line().line();
    int last_line = buffer->contents()->size();

    int begin = 0;
    int end = last_line;
    switch (modifiers_.structure_range) {
      case Modifiers::ENTIRE_STRUCTURE:
        break;  // We're all set.
      case Modifiers::FROM_BEGINNING_TO_CURRENT_POSITION:
        end = current_line;
        break;
      case Modifiers::FROM_CURRENT_POSITION_TO_END:
        begin = current_line;
        break;
    }

    CHECK_LE(begin, end);
    // TODO(alejo): Handle reverse?
    Modifiers modifiers;
    modifiers.repetitions = end - begin;
    TransformationAtPosition(LineColumn(begin),
        NewDeleteLinesTransformation(modifiers, copy_to_paste_buffer_))
            ->Apply(editor_state, buffer, result);
  }

  unique_ptr<Transformation> Clone() {
    return NewDeleteBufferTransformation(modifiers_, copy_to_paste_buffer_);
  }

 private:
  const Modifiers& modifiers_;
  bool copy_to_paste_buffer_;
};

class DeleteTransformation : public Transformation {
 public:
  DeleteTransformation(const Modifiers& modifiers, bool copy_to_paste_buffer)
      : modifiers_(modifiers),
        copy_to_paste_buffer_(copy_to_paste_buffer) {}

  void Apply(
      EditorState* editor_state, OpenBuffer* buffer, Result* result) const {
    unique_ptr<Transformation> delegate;
    switch (modifiers_.structure) {
      case CHAR:
        delegate = NewDeleteCharactersTransformation(
            modifiers_, copy_to_paste_buffer_);
        break;
      case WORD:
        delegate = NewDeleteWordsTransformation(
            modifiers_, copy_to_paste_buffer_);
        break;
      case LINE:
        delegate = NewDeleteLinesTransformation(
            modifiers_, copy_to_paste_buffer_);
        break;
      case BUFFER:
        delegate = NewDeleteBufferTransformation(
            modifiers_, copy_to_paste_buffer_);
        break;
      default:
        LOG(INFO) << "DeleteTransformation can't handle structure: "
                  << modifiers_.structure;
        delegate = NewNoopTransformation();
    }
    return delegate->Apply(editor_state, buffer, result);
  }

  unique_ptr<Transformation> Clone() {
    return NewDeleteTransformation(modifiers_, copy_to_paste_buffer_);
  }

 private:
  Modifiers modifiers_;
  bool copy_to_paste_buffer_;
};

}  // namespace

unique_ptr<Transformation> NewDeleteCharactersTransformation(
    const Modifiers& modifiers, bool copy_to_paste_buffer) {
  return unique_ptr<Transformation>(
      new DeleteCharactersTransformation(modifiers, copy_to_paste_buffer));
}

unique_ptr<Transformation> NewDeleteWordsTransformation(
    const Modifiers& modifiers, bool copy_to_paste_buffer) {
  Modifiers modifiers_sans_repetitions(modifiers);
  modifiers_sans_repetitions.repetitions = 1;
  return NewApplyRepetitionsTransformation(modifiers.repetitions,
      unique_ptr<Transformation>(new DeleteWordTransformation(
          modifiers_sans_repetitions, copy_to_paste_buffer)));
}

unique_ptr<Transformation> NewDeleteLinesTransformation(
    const Modifiers& modifiers, bool copy_to_paste_buffer) {
  return unique_ptr<Transformation>(
      new DeleteLinesTransformation(modifiers, copy_to_paste_buffer));
}

unique_ptr<Transformation> NewDeleteBufferTransformation(
    const Modifiers& modifiers, bool copy_to_paste_buffer) {
  return unique_ptr<Transformation>(
      new DeleteBufferTransformation(modifiers, copy_to_paste_buffer));
}

unique_ptr<Transformation> NewDeleteTransformation(
    const Modifiers& modifiers, bool copy_to_paste_buffer) {
  return unique_ptr<Transformation>(
      new DeleteTransformation(modifiers, copy_to_paste_buffer));
}

}  // namespace editor
}  // namespace afc
