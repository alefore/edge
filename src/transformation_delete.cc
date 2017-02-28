#include "transformation_delete.h"

#include <glog/logging.h>

#include "buffer.h"
#include "direction.h"
#include "editor.h"
#include "lazy_string_append.h"
#include "modifiers.h"
#include "transformation.h"
#include "transformation_move.h"
#include "wstring.h"

namespace afc {
namespace editor {

namespace {
class DeleteCharactersTransformation : public Transformation {
 public:
  DeleteCharactersTransformation(DeleteOptions options) : options_(options) {}

  void Apply(
      EditorState* editor_state, OpenBuffer* buffer, Result* result) const {
    LOG(INFO) << "Starting DeleteCharactersTransformation: "
              << options_.modifiers << ", cursor: " << result->cursor;
    if (buffer->contents()->empty()) {
      result->success = false;
      return;
    }
    if (options_.modifiers.repetitions == 0) {
      VLOG(5) << "No repetitions.";
      return;
    }
    buffer->AdjustLineColumn(&result->cursor);
    size_t current_line = result->cursor.line;
    if (buffer->LineAt(current_line) == nullptr) {
      result->made_progress = false;
      return;
    }

    shared_ptr<LazyString> preserved_contents =
        StartOfLine(buffer, current_line, result->cursor.column,
                    options_.modifiers.direction);

    size_t line;
    size_t chars_erased;
    SkipLinesToErase(buffer, preserved_contents, &line, &chars_erased,
                     result->cursor);
    LOG(INFO) << "Erasing from line " << current_line << " to line " << line
              << " would erase " << chars_erased << " characters.";

    // The amount of characters that should be erased from the current line.
    // Depending on the direction, we'll erase from the beginning (FORWARDS) or
    // the end of the current line (BACKWARDS).  If the line is the current
    // line, this already includes characters in preserved_contents.
    size_t chars_erase_line = buffer->LineAt(line)->size()
        + (options_.modifiers.direction == FORWARDS ? 1 : 0)
        - min(buffer->LineAt(line)->size(),
              (options_.modifiers.repetitions < chars_erased
                   ? chars_erased - options_.modifiers.repetitions
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
              << ", modifiers: " << options_.modifiers << ", chars_erased: "
              << chars_erased << ", preserved_contents size: "
              << preserved_contents->size() << ", actual length: "
              << buffer->LineAt(line)->size();

    result->success = chars_erased >= options_.modifiers.repetitions;
    result->made_progress = chars_erased + chars_erase_line > 0;

    size_t line_begin = min(line, current_line);
    size_t line_end = max(line, current_line);

    shared_ptr<OpenBuffer> delete_buffer = GetDeletedTextBuffer(
        editor_state, buffer, line_begin, line_end, preserved_contents,
        chars_erase_line);
    if (options_.copy_to_paste_buffer) {
      VLOG(5) << "Preparing delete buffer.";
      result->delete_buffer->Apply(
          editor_state, NewInsertBufferTransformation(delete_buffer, 1, END),
          result->delete_buffer->position());
    }

    if (options_.modifiers.direction == BACKWARDS) {
      result->cursor.line = line;
      result->cursor.column = buffer->LineAt(line)->size() - chars_erase_line;
    }

    LOG(INFO) << "Storing new line (at position " << max(current_line, line)
              << ").";
    auto initial_line = buffer->LineAt(line);
    Line::Options options;
    switch (options_.modifiers.direction) {
      case FORWARDS:
        options.contents = StringAppend(
            preserved_contents, initial_line->Substring(chars_erase_line));
        buffer->AdjustCursors(
            [line, preserved_contents, chars_erase_line](LineColumn cursor) {
              if (cursor.line == line) {
                if (cursor.column > chars_erase_line) {
                  cursor.column += preserved_contents->size() - chars_erase_line;
                } else if (cursor.column > preserved_contents->size()) {
                  cursor.column -= preserved_contents->size();
                }
              }
              return cursor;
            });
        break;
      case BACKWARDS:
        options.contents = StringAppend(
            initial_line->Substring(0, initial_line->size() - chars_erase_line),
            preserved_contents);
        buffer->AdjustCursors(
            [line, preserved_contents, chars_erase_line, initial_line](
                LineColumn cursor) {
              if (cursor.line == line
                  && cursor.column > initial_line->size() - chars_erase_line) {
                cursor.column =
                    max(cursor.column - (chars_erase_line - preserved_contents->size()),
                        initial_line->size() - chars_erase_line);
              }
              return cursor;
            });
        break;
    }
    buffer->ReplaceLine(
        buffer->contents()->begin() + line_end,
        std::make_shared<Line>(options));

    buffer->EraseLines(buffer->contents()->begin() + line_begin,
                       buffer->contents()->begin() + line_end);
    result->modified_buffer = true;

    result->undo = TransformationAtPosition(result->cursor,
        NewInsertBufferTransformation(
            delete_buffer, 1,
            options_.modifiers.direction == FORWARDS ? START : END));
  }

  unique_ptr<Transformation> Clone() {
    return NewDeleteCharactersTransformation(options_);
  }

 private:
  shared_ptr<OpenBuffer> GetDeletedTextBuffer(
      EditorState* editor_state, OpenBuffer* buffer, size_t line_begin,
      size_t line_end, const shared_ptr<LazyString>& preserved_contents,
      size_t chars_erase_line) const {
    LOG(INFO) << "Preparing deleted text buffer.";
    auto delete_buffer =
        std::make_shared<OpenBuffer>(editor_state, OpenBuffer::kPasteBuffer);

    if (line_begin == line_end) {
      auto end_line = buffer->LineAt(line_begin);
      size_t start = options_.modifiers.direction == FORWARDS
          ? preserved_contents->size()
          : end_line->size() - chars_erase_line;
      CHECK_LE(start, end_line->size());
      size_t end = options_.modifiers.direction == FORWARDS
          ? chars_erase_line
          : end_line->size() - preserved_contents->size();
      CHECK_LE(start, end);
      CHECK_LE(end, end_line->size());
      LOG(INFO) << "Preserving chars from single line: [" << start << ", "
                << end << "): "
                << end_line->Substring(start, end - start)->ToString();
      delete_buffer->AppendToLastLine(
          editor_state, end_line->Substring(start, end - start));
      return delete_buffer;
    }

    delete_buffer->AppendToLastLine(editor_state,
        buffer->LineAt(line_begin)->Substring(
            options_.modifiers.direction == FORWARDS
            ? preserved_contents->size()
            : buffer->LineAt(line_begin)->size() - chars_erase_line));

    for (size_t i = line_begin + 1; i < line_end; i++) {
      delete_buffer->AppendLine(
          editor_state, buffer->LineAt(i)->contents());
    }

    delete_buffer->AppendLine(editor_state,
        buffer->LineAt(line_end)->Substring(0,
            options_.modifiers.direction == FORWARDS
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
    }
    CHECK(false);
    return nullptr;
  }

  // Loop away from the current line (in the direction given), stopping at the
  // first line such that if we erase all characters in it (including \n), we
  // will have erased at least as many characters as needed.
  //
  // chars_erased will be set to the total number of characters erased from the
  // current position until (including) line.
  void SkipLinesToErase(const OpenBuffer* buffer,
                        const shared_ptr<LazyString>& preserved_contents,
                        size_t* line, size_t* chars_erased,
                        LineColumn position) const {
    *line = position.line;
    *chars_erased = 0;
    if (options_.modifiers.direction == FORWARDS
        && *line == buffer->contents()->size()) {
      return;
    }

    while (true) {
      CHECK_LT(*line, buffer->contents()->size());
      LOG(INFO) << "Iteration at line " << *line << " having already erased "
                << *chars_erased << " characters.";
      size_t chars_in_line = buffer->LineAt(*line)->size();
      if (*line == position.line) {
        CHECK_GE(chars_in_line, preserved_contents->size());
        chars_in_line -= preserved_contents->size();
        if (options_.modifiers.direction == FORWARDS) {
          chars_in_line++;
        }
      } else if (*line + 1 < buffer->contents()->size()) {
        chars_in_line++;  // The new line character.
      }
      LOG(INFO) << "Characters available in line: " << chars_in_line;
      *chars_erased += chars_in_line;
      if (*chars_erased >= options_.modifiers.repetitions) {
        return;
      }
      CHECK_LT(*chars_erased, options_.modifiers.repetitions);

      if (!AdvanceLine(buffer, line)) {
        return;
      }
    }
  }

  bool AdvanceLine(const OpenBuffer* buffer, size_t* line) const {
    size_t old_value = *line;
    switch (options_.modifiers.direction) {
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

  const DeleteOptions options_;
};

class DeleteRegionTransformation : public Transformation {
 public:
  DeleteRegionTransformation(DeleteOptions options) : options_(options) {}

  void Apply(
      EditorState* editor_state, OpenBuffer* buffer, Result* result) const {
    CHECK(buffer != nullptr);
    CHECK(result != nullptr);

    LineColumn start, end;
    if (!buffer->FindPartialRange(options_.modifiers, result->cursor, &start,
                                  &end)) {
      result->success = false;
      LOG(INFO) << "Unable to bind region, giving up.";
      return;
    }

    LOG(INFO) << "Starting at " << result->cursor << ", bound region at ["
              << start << ", " << end << ")";

    start = min(start, result->cursor);
    end = max(end, result->cursor);

    CHECK_LE(start, end);

    TransformationStack stack;

    stack.PushBack(NewGotoPositionTransformation(start));
    if (start.line < end.line) {
      LOG(INFO) << "Deleting superfluous lines (from " << start.line << " to "
                << end.line;
      while (start.line < end.line) {
        end.line--;
        DeleteOptions delete_options;
        delete_options.modifiers.structure_range =
            Modifiers::FROM_CURRENT_POSITION_TO_END;
        stack.PushBack(NewDeleteLinesTransformation(delete_options));
      }
      end.column += start.column;
    }

    CHECK_LE(start, end);
    CHECK_LE(start.column, end.column);
    DeleteOptions delete_options;
    delete_options.modifiers.repetitions = end.column - start.column;
    LOG(INFO) << "Deleting characters: " << options_.modifiers.repetitions;
    stack.PushBack(NewDeleteCharactersTransformation(delete_options));
    stack.Apply(editor_state, buffer, result);
  }

  unique_ptr<Transformation> Clone() {
    return NewDeleteRegionTransformation(options_);
  }

 private:
  const DeleteOptions options_;
};

class DeleteLinesTransformation : public Transformation {
 public:
   DeleteLinesTransformation(DeleteOptions options) : options_(options) {}

  void Apply(
      EditorState* editor_state, OpenBuffer* buffer, Result* result) const {
    CHECK(buffer != nullptr);
    buffer->AdjustLineColumn(&result->cursor);
    size_t repetitions = min(options_.modifiers.repetitions,
                             buffer->contents()->size() - result->cursor.line);
    shared_ptr<OpenBuffer> delete_buffer(
        new OpenBuffer(editor_state, OpenBuffer::kPasteBuffer));

    LOG(INFO) << "Erasing lines " << repetitions << " starting at line "
         << result->cursor.line << " in a buffer with size "
         << buffer->contents()->size() << " with modifiers: "
         << options_.modifiers;

    bool forwards = options_.modifiers.structure_range
        != Modifiers::FROM_BEGINNING_TO_CURRENT_POSITION;
    bool backwards = options_.modifiers.structure_range
        != Modifiers::FROM_CURRENT_POSITION_TO_END;

    bool deletes_ends_of_lines = options_.modifiers.strength > Modifiers::WEAK;

    TransformationStack stack;

    size_t line = result->cursor.line;
    for (size_t i = 0; i < repetitions; i++) {
      auto contents = buffer->LineAt(line + i);
      DVLOG(5) << "Erasing line: " << contents->ToString();
      size_t start = backwards ? 0 : result->cursor.column;
      size_t end = forwards ? contents->size() : result->cursor.column;
      if (start == 0 && end == contents->size()) {
        auto target_buffer = buffer->GetBufferFromCurrentLine();
        if (target_buffer.get() != buffer && target_buffer != nullptr) {
          auto it = editor_state->buffers()->find(target_buffer->name());
          if (it != editor_state->buffers()->end()) {
            editor_state->CloseBuffer(it);
          }
        }

        if (buffer->LineAt(result->cursor.line) != nullptr) {
          auto callback =
              buffer->LineAt(result->cursor.line)
                  ->environment()
                  ->Lookup(L"EdgeLineDeleteHandler");
          if (callback != nullptr
              && callback->type.type == vm::VMType::FUNCTION
              && callback->type.type_arguments.size() == 1
              && callback->type.type_arguments.at(0) == vm::VMType::VM_VOID) {
            LOG(INFO) << "Running EdgeLineDeleteHandler.";
            callback->callback({});
          }
        }
      }
      DeleteOptions delete_options;
      delete_options.modifiers.repetitions = end - start
          + (deletes_ends_of_lines && end == contents->size() ? 1 : 0);
      LineColumn position(line + (deletes_ends_of_lines ? 0 : i), start);
      DVLOG(6) << "Modifiers for line: " << delete_options.modifiers;
      DVLOG(6) << "Position for line: " << position;
      stack.PushBack(
          TransformationAtPosition(position,
               NewDeleteCharactersTransformation(delete_options)));
    }
    if (editor_state->has_current_buffer()
        && editor_state->current_buffer()->first == OpenBuffer::kBuffersName
        && !editor_state->status_prompt()) {
      LOG(INFO) << "Updating list of buffers: " << OpenBuffer::kBuffersName;
      auto buffers_list = editor_state->current_buffer()->second;
      buffers_list->Reload(editor_state);
      buffers_list->AdjustLineColumn(&result->cursor);
    } else {
      stack.Apply(editor_state, buffer, result);
    }
  }

  unique_ptr<Transformation> Clone() {
    return NewDeleteLinesTransformation(options_);
  }

 private:
  size_t FindStartOfLine(OpenBuffer* buffer, const Line* line) const {
    if (options_.modifiers.strength == Modifiers::VERY_WEAK) {
      return FindSoftStartOfLine(buffer, line);
    }
    return 0;
  }
  static size_t FindSoftStartOfLine(OpenBuffer* buffer, const Line* line) {
    const wstring& word_chars =
        buffer->read_string_variable(buffer->variable_word_characters());
    size_t start = 0;
    while (start < line->size()
           && word_chars.find(line->get(start)) == wstring::npos) {
      start++;
    }
    return start;
  }

  size_t FindLengthOfLine(OpenBuffer* buffer, const Line* line) const {
    if (options_.modifiers.strength == Modifiers::VERY_WEAK) {
      return FindSoftLengthOfLine(buffer, line);
    }
    return line->size();
  }

  static size_t FindSoftLengthOfLine(OpenBuffer* buffer, const Line* line) {
    const wstring& word_chars =
        buffer->read_string_variable(buffer->variable_word_characters());
    size_t length = line->size();
    while (length > 0
           && word_chars.find(line->get(length - 1)) == wstring::npos) {
      length--;
    }
    return length;
  }

  DeleteOptions options_;
};

class DeleteBufferTransformation : public Transformation {
 public:
  DeleteBufferTransformation(DeleteOptions options) : options_(options) {}

  void Apply(
      EditorState* editor_state, OpenBuffer* buffer, Result* result) const {
    LOG(INFO) << "Erasing buffer (modifiers: " << options_.modifiers << ") of size: "
              << buffer->contents()->size();

    int current_line = result->cursor.line;
    int last_line = buffer->contents()->size();

    int begin = 0;
    int end = last_line;
    switch (options_.modifiers.structure_range) {
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
    DeleteOptions options = options_;
    options.modifiers.repetitions = end - begin;
    TransformationAtPosition(LineColumn(begin),
        NewDeleteLinesTransformation(options))
            ->Apply(editor_state, buffer, result);
  }

  unique_ptr<Transformation> Clone() {
    return NewDeleteBufferTransformation(options_);
  }

 private:
  const DeleteOptions options_;
};

class DeleteTransformation : public Transformation {
 public:
  DeleteTransformation(DeleteOptions options) : options_(options) {}

  void Apply(
      EditorState* editor_state, OpenBuffer* buffer, Result* result) const {
    LOG(INFO) << "Start delete transformation at " << result->cursor << ": "
              << options_.modifiers;
    unique_ptr<Transformation> delegate = NewNoopTransformation();
    switch (options_.modifiers.structure) {
      case CHAR:
        delegate = NewDeleteCharactersTransformation(options_);
        break;
      case WORD:
      case CURSOR:
      case TREE:
        delegate = NewDeleteRegionTransformation(options_);
        break;
      case LINE:
        delegate = NewDeleteLinesTransformation(options_);
        break;
      case BUFFER:
        delegate = NewDeleteBufferTransformation(options_);
        break;
      case MARK:
      case PAGE:
      case SEARCH:
        LOG(INFO) << "DeleteTransformation can't handle structure: "
                  << options_.modifiers.structure;
        break;
    }
    return delegate->Apply(editor_state, buffer, result);
  }

  unique_ptr<Transformation> Clone() {
    return NewDeleteTransformation(options_);
  }

 private:
  const DeleteOptions options_;
};

}  // namespace

unique_ptr<Transformation> NewDeleteCharactersTransformation(
    DeleteOptions options) {
  return unique_ptr<Transformation>(
      new DeleteCharactersTransformation(options));
}

unique_ptr<Transformation> NewDeleteRegionTransformation(
    DeleteOptions options) {
  return unique_ptr<Transformation>(new DeleteRegionTransformation(options));
}

unique_ptr<Transformation> NewDeleteLinesTransformation(DeleteOptions options) {
  return unique_ptr<Transformation>(new DeleteLinesTransformation(options));
}

unique_ptr<Transformation> NewDeleteBufferTransformation(
    DeleteOptions options) {
  return unique_ptr<Transformation>(new DeleteBufferTransformation(options));
}

unique_ptr<Transformation> NewDeleteTransformation(DeleteOptions options) {
  return unique_ptr<Transformation>(new DeleteTransformation(options));
}

}  // namespace editor
}  // namespace afc
