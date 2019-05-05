#include "src/transformation_delete.h"

#include <glog/logging.h>

#include "src/buffer.h"
#include "src/buffer_variables.h"
#include "src/direction.h"
#include "src/editor.h"
#include "src/lazy_string_append.h"
#include "src/modifiers.h"
#include "src/transformation.h"
#include "src/transformation_move.h"
#include "src/vm/public/constant_expression.h"
#include "src/vm/public/environment.h"
#include "src/vm/public/function_call.h"
#include "src/wstring.h"

namespace afc {
namespace editor {

std::ostream& operator<<(std::ostream& os, const DeleteOptions& options) {
  os << "[DeleteOptions: copy_to_paste_buffer:" << options.copy_to_paste_buffer
     << ", modifiers:" << options.modifiers << "]";
  return os;
}

namespace {
class DeleteCharactersTransformation : public Transformation {
 public:
  static std::unique_ptr<Transformation> New(DeleteOptions options) {
    return std::make_unique<DeleteCharactersTransformation>(options);
  }

  DeleteCharactersTransformation(DeleteOptions options) : options_(options) {}

  void Apply(OpenBuffer* buffer, Result* result) const override {
    LOG(INFO) << "Starting DeleteCharactersTransformation: " << options_
              << ", cursor: " << result->cursor;
    if (options_.modifiers.repetitions == 0) {
      VLOG(5) << "No repetitions.";
      return;
    }
    const auto original_position = result->cursor;
    buffer->AdjustLineColumn(&result->cursor);
    if (options_.modifiers.direction == BACKWARDS) {
      for (size_t i = 0; i < options_.modifiers.repetitions; i++) {
        result->cursor = buffer->PositionBefore(result->cursor);
      }
    }

    if (buffer->LineAt(result->cursor.line) == nullptr) {
      LOG(INFO) << "Can't make progress: Empty line.";
      result->made_progress = false;
      return;
    }

    size_t chars_erased;
    LineNumber line_end;
    if (options_.line_end_behavior == DeleteOptions::LineEndBehavior::kDelete) {
      line_end = SkipLinesToErase(
          buffer, result->cursor.column.column + options_.modifiers.repetitions,
          result->cursor.line, &chars_erased);
    } else {
      line_end = result->cursor.line;
      chars_erased = buffer->LineAt(result->cursor.line)->size() + 1;
    }
    LOG(INFO) << "Erasing from line " << result->cursor.line << " to line "
              << line_end << " would erase " << chars_erased << " characters.";
    chars_erased -= result->cursor.column.column;

    // The amount of characters that should be erased from the current line. If
    // the line is the current line, this already includes characters in the
    // prefix.
    size_t chars_erase_line =
        buffer->LineAt(line_end)->size() + 1 -
        min(buffer->LineAt(line_end)->size(),
            (options_.modifiers.repetitions < chars_erased
                 ? chars_erased - options_.modifiers.repetitions
                 : 0));
    if (chars_erase_line > buffer->LineAt(line_end)->size()) {
      LOG(INFO) << "Adjusting for end of buffer.";
      CHECK_EQ(chars_erase_line, buffer->LineAt(line_end)->size() + 1);
      chars_erase_line = 0;
      if (line_end >= buffer->EndLine() ||
          options_.line_end_behavior == DeleteOptions::LineEndBehavior::kStop) {
        chars_erase_line = buffer->LineAt(line_end)->size();
      } else {
        ++line_end;
      }
    }
    LOG(INFO) << "Characters to erase from current line: " << chars_erase_line
              << ", options: " << options_ << ", chars_erased: " << chars_erased
              << ", actual length: " << buffer->LineAt(line_end)->size();

    result->success = chars_erased >= options_.modifiers.repetitions;
    result->made_progress = chars_erased + chars_erase_line > 0;

    shared_ptr<OpenBuffer> delete_buffer = GetDeletedTextBuffer(
        buffer, result->cursor, line_end, ColumnNumber(chars_erase_line));
    if (options_.copy_to_paste_buffer &&
        result->mode == Transformation::Result::Mode::kFinal) {
      VLOG(5) << "Preparing delete buffer.";
      InsertOptions insert_options;
      insert_options.buffer_to_insert = delete_buffer;
      result->delete_buffer->ApplyToCursors(TransformationAtPosition(
          result->delete_buffer->position(),
          NewInsertBufferTransformation(std::move(insert_options))));
    }

    if (options_.modifiers.delete_type == Modifiers::PRESERVE_CONTENTS &&
        result->mode == Transformation::Result::Mode::kFinal) {
      LOG(INFO) << "Not actually deleting region.";
      result->cursor = original_position;
      return;
    }

    LOG(INFO) << "Storing new line (at position " << line_end << ").";
    buffer->DeleteRange(Range(
        result->cursor, LineColumn(line_end, ColumnNumber(chars_erase_line))));

    result->modified_buffer = true;

    {
      InsertOptions insert_options;
      insert_options.buffer_to_insert = delete_buffer;
      insert_options.final_position = options_.modifiers.direction == FORWARDS
                                          ? InsertOptions::FinalPosition::kStart
                                          : InsertOptions::FinalPosition::kEnd;
      result->undo_stack->PushFront(TransformationAtPosition(
          result->cursor,
          NewInsertBufferTransformation(std::move(insert_options))));
    }

    if (result->mode == Transformation::Result::Mode::kPreview) {
      LOG(INFO) << "Inserting preview at: " << result->cursor << " "
                << delete_buffer->contents()->CountCharacters();
      LineModifierSet modifiers_set = {LineModifier::UNDERLINE,
                                       LineModifier::BLUE};
      InsertOptions insert_options;
      insert_options.buffer_to_insert = delete_buffer;
      insert_options.final_position =
          options_.modifiers.direction == BACKWARDS
              ? InsertOptions::FinalPosition::kEnd
              : InsertOptions::FinalPosition::kStart;
      insert_options.modifiers_set = &modifiers_set;
      NewInsertBufferTransformation(std::move(insert_options))
          ->Apply(buffer, result);
    }
  }

  unique_ptr<Transformation> Clone() const override {
    return DeleteCharactersTransformation::New(options_);
  }

 private:
  // If modifiers is null, the original modifiers (from the input buffer) are
  // used. Otherwise, they're overridden by modifiers.
  shared_ptr<OpenBuffer> GetDeletedTextBuffer(
      OpenBuffer* buffer, LineColumn begin, LineNumber line_end,
      ColumnNumber chars_erase_line) const {
    LOG(INFO) << "Preparing deleted text buffer.";
    auto delete_buffer = std::make_shared<OpenBuffer>(buffer->editor(),
                                                      OpenBuffer::kPasteBuffer);
    Line::Options first_line(*buffer->LineAt(begin.line));
    if (begin.line == line_end) {
      first_line.DeleteSuffix(chars_erase_line);
    }
    first_line.DeleteCharacters(ColumnNumber(0), begin.column.ToDelta());
    delete_buffer->AppendToLastLine(Line(std::move(first_line)));

    for (LineNumber i = begin.line.next(); i <= line_end; ++i) {
      Line::Options replacement(*buffer->LineAt(i));
      if (i == line_end) {
        replacement.DeleteSuffix(chars_erase_line);
      }
      delete_buffer->AppendRawLine(std::make_shared<Line>(replacement));
    }

    return delete_buffer;
  }

  // Find and return the nearest (to line) line `line` such that if we erase all
  // characters in every line (including \n separators) between the current line
  // and `line` (including both), we will have erased at least as may characters
  // as chars_to_erase.
  //
  // chars_erased will be set to the total number of characters erased from the
  // current position until (including) line.
  LineNumber SkipLinesToErase(const OpenBuffer* buffer, size_t chars_to_erase,
                              LineNumber line, size_t* chars_erased) const {
    *chars_erased = 0;
    if (line == LineNumber(0) + buffer->contents()->size()) {
      return line;
    }
    auto newlines = 1;
    while (true) {
      CHECK_LE(line, buffer->contents()->EndLine());
      LOG(INFO) << "Iteration at line " << line << " having already erased "
                << *chars_erased << " characters.";
      size_t chars_in_line = buffer->LineAt(line)->size() + newlines;
      LOG(INFO) << "Characters available in line: " << chars_in_line;
      *chars_erased += chars_in_line;
      if (*chars_erased >= chars_to_erase) {
        return line;
      }
      if (line >= buffer->EndLine()) {
        return line;
      }
      line++;
      newlines = 1;
    }
  }

  const DeleteOptions options_;
};

class DeleteLinesTransformation : public Transformation {
 public:
  DeleteLinesTransformation(DeleteOptions options) : options_(options) {}

  void Apply(OpenBuffer* buffer, Result* result) const {
    CHECK(buffer != nullptr);
    buffer->AdjustLineColumn(&result->cursor);
    const LineColumn adjusted_original_cursor = result->cursor;
    CHECK_GE(buffer->contents()->size(), result->cursor.line.ToDelta());
    size_t repetitions = min(options_.modifiers.repetitions,
                             static_cast<size_t>((buffer->contents()->size() -
                                                  result->cursor.line.ToDelta())
                                                     .line_delta));
    auto delete_buffer = std::make_shared<OpenBuffer>(buffer->editor(),
                                                      OpenBuffer::kPasteBuffer);

    LOG(INFO) << "Erasing lines " << repetitions << " starting at line "
              << result->cursor.line << " in a buffer with size "
              << buffer->contents()->size()
              << " with modifiers: " << options_.modifiers;

    bool forwards = options_.modifiers.structure_range !=
                    Modifiers::FROM_BEGINNING_TO_CURRENT_POSITION;
    bool backwards = options_.modifiers.structure_range !=
                     Modifiers::FROM_CURRENT_POSITION_TO_END;

    TransformationStack stack;

    LineNumber line = result->cursor.line;
    for (size_t i = 0; i < repetitions; i++) {
      auto contents = buffer->LineAt(line + LineNumberDelta(i));
      DVLOG(5) << "Erasing line: " << contents->ToString();
      ColumnNumber start = backwards ? ColumnNumber(0) : result->cursor.column;
      ColumnNumber end =
          forwards ? contents->EndColumn() : result->cursor.column;
      if (start == ColumnNumber(0) && end == contents->EndColumn() &&
          options_.modifiers.delete_type == Modifiers::DELETE_CONTENTS &&
          result->mode == Transformation::Result::Mode::kFinal) {
        auto target_buffer = buffer->GetBufferFromCurrentLine();
        if (target_buffer.get() != buffer && target_buffer != nullptr) {
          target_buffer->editor()->CloseBuffer(target_buffer.get());
        }

        if (buffer->LineAt(result->cursor.line) != nullptr) {
          Value* callback = buffer->LineAt(result->cursor.line)
                                ->environment()
                                ->Lookup(L"EdgeLineDeleteHandler",
                                         VMType::Function({VMType::Void()}));
          if (callback != nullptr) {
            LOG(INFO) << "Running EdgeLineDeleteHandler.";
            std::shared_ptr<Expression> expr = vm::NewFunctionCall(
                vm::NewConstantExpression(std::make_unique<Value>(*callback)),
                {});
            Evaluate(
                expr.get(), buffer->environment(), [expr](Value::Ptr) {},
                [target_buffer](std::function<void()> callback) {
                  target_buffer->SchedulePendingWork(callback);
                });
          }
        }
      }
      DeleteOptions delete_options;
      delete_options.copy_to_paste_buffer = options_.copy_to_paste_buffer;
      delete_options.modifiers.delete_type = options_.modifiers.delete_type;
      delete_options.modifiers.repetitions =
          (end - start +
           ColumnNumberDelta(end == contents->EndColumn() ? 1 : 0))
              .column_delta;
      LineColumn position(line, start);
      if (options_.modifiers.delete_type == Modifiers::PRESERVE_CONTENTS ||
          result->mode == Transformation::Result::Mode::kPreview) {
        position.line += LineNumberDelta(i);
      }
      DVLOG(6) << "Modifiers for line: " << delete_options.modifiers;
      DVLOG(6) << "Position for line: " << position;
      stack.PushBack(TransformationAtPosition(
          position, DeleteCharactersTransformation::New(delete_options)));
    }
    if (options_.modifiers.delete_type == Modifiers::PRESERVE_CONTENTS ||
        result->mode == Transformation::Result::Mode::kPreview) {
      stack.PushBack(NewGotoPositionTransformation(adjusted_original_cursor));
    }
    stack.Apply(buffer, result);
  }

  unique_ptr<Transformation> Clone() const override {
    return std::make_unique<DeleteLinesTransformation>(options_);
  }

 private:
  const DeleteOptions options_;
};

class DeleteTransformation : public Transformation {
 public:
  DeleteTransformation(DeleteOptions options) : options_(options) {}

  void Apply(OpenBuffer* buffer, Result* result) const {
    CHECK(buffer != nullptr);
    CHECK(result != nullptr);

    buffer->AdjustLineColumn(&result->cursor);
    const LineColumn adjusted_original_cursor = result->cursor;

    Range range = buffer->FindPartialRange(options_.modifiers, result->cursor);
    LOG(INFO) << "Starting at " << result->cursor << ", bound region at "
              << range;

    range.begin = min(range.begin, result->cursor);
    range.end = max(range.end, result->cursor);

    CHECK_LE(range.begin, range.end);

    TransformationStack stack;
    stack.PushBack(NewGotoPositionTransformation(range.begin));
    if (range.begin.line < range.end.line) {
      LOG(INFO) << "Deleting superfluous lines (from " << range << ")";
      while (range.begin.line < range.end.line) {
        DeleteOptions delete_options;
        delete_options.modifiers.delete_type = options_.modifiers.delete_type;
        delete_options.modifiers.structure_range =
            Modifiers::FROM_CURRENT_POSITION_TO_END;
        delete_options.copy_to_paste_buffer = options_.copy_to_paste_buffer;
        stack.PushBack(TransformationAtPosition(
            range.begin, std::make_unique<DeleteLinesTransformation>(
                             std::move(delete_options))));
        if (options_.modifiers.delete_type == Modifiers::DELETE_CONTENTS &&
            result->mode == Transformation::Result::Mode::kFinal) {
          range.end.line--;
        } else {
          range.begin.line++;
          range.begin.column = ColumnNumber(0);
        }
      }
      range.end.column += range.begin.column.ToDelta();
    }

    CHECK_LE(range.begin, range.end);
    CHECK_LE(range.begin.column, range.end.column);
    DeleteOptions delete_options;
    delete_options.copy_to_paste_buffer = options_.copy_to_paste_buffer;
    delete_options.modifiers.repetitions =
        (range.end.column - range.begin.column).column_delta;
    delete_options.modifiers.delete_type = options_.modifiers.delete_type;
    LOG(INFO) << "Deleting characters at: " << range.begin << ": "
              << options_.modifiers.repetitions;
    stack.PushBack(TransformationAtPosition(
        range.begin, DeleteCharactersTransformation::New(delete_options)));
    if (options_.modifiers.delete_type == Modifiers::PRESERVE_CONTENTS) {
      stack.PushBack(NewGotoPositionTransformation(adjusted_original_cursor));
    } else {
      stack.PushBack(std::make_unique<RunIfModeTransformation>(
          Transformation::Result::Mode::kPreview,
          NewGotoPositionTransformation(adjusted_original_cursor)));
    }
    stack.Apply(buffer, result);
  }

  unique_ptr<Transformation> Clone() const override {
    return NewDeleteTransformation(options_);
  }

 private:
  const DeleteOptions options_;
};

}  // namespace

std::unique_ptr<Transformation> NewDeleteTransformation(DeleteOptions options) {
  return std::make_unique<DeleteTransformation>(options);
}

}  // namespace editor
}  // namespace afc
