#include "insert_mode.h"

#include <cassert>
#include <memory>

extern "C" {
#include <unistd.h>
}

#include <glog/logging.h>

#include "char_buffer.h"
#include "command_mode.h"
#include "editable_string.h"
#include "editor.h"
#include "file_link_mode.h"
#include "lazy_string_append.h"
#include "substring.h"
#include "terminal.h"

namespace {
using namespace afc::editor;

class NewLineTransformation : public Transformation {
  unique_ptr<Transformation> Apply(
      EditorState* editor_state, OpenBuffer* buffer) const {
    buffer->MaybeAdjustPositionCol();
    const size_t column = buffer->position().column;
    auto current_line = buffer->current_line();

    if (buffer->read_bool_variable(OpenBuffer::variable_atomic_lines())
        && column != 0
        && (current_line == nullptr || column != current_line->size())) {
      return NewNoopTransformation();
    }

    const string& line_prefix_characters(buffer->read_string_variable(
        OpenBuffer::variable_line_prefix_characters()));
    size_t prefix_end = 0;
    if (current_line != nullptr
        && !buffer->read_bool_variable(OpenBuffer::variable_paste_mode())) {
      while (prefix_end < column
             && (line_prefix_characters.find(current_line->get(prefix_end))
                 != line_prefix_characters.npos)) {
        prefix_end++;
      }
    }

    Line::Options continuation_options;
    if (current_line != nullptr) {
      continuation_options.contents = StringAppend(
          current_line->Substring(0, prefix_end),
          current_line->Substring(column));
    }

    unique_ptr<TransformationStack> transformation(new TransformationStack);

    if (current_line != nullptr && column < current_line->size()) {
      transformation->PushBack(NewDeleteCharactersTransformation(
          current_line->size() - column,
          false));
    }
    transformation->PushBack(NewDeleteSuffixSuperfluousCharacters());

    {
      shared_ptr<OpenBuffer> buffer_to_insert(
        new OpenBuffer(editor_state, "- text inserted"));
      buffer_to_insert->contents()->emplace_back(new Line(Line::Options()));
      buffer_to_insert->contents()
          ->emplace_back(new Line(continuation_options));
      transformation->PushBack(
          NewInsertBufferTransformation(buffer_to_insert, 1, END));
    }
    transformation->PushBack(NewGotoPositionTransformation(
        LineColumn(buffer->position().line + 1, prefix_end)));
    return transformation->Apply(editor_state, buffer);
  }

  unique_ptr<Transformation> Clone() {
    return unique_ptr<Transformation>(new NewLineTransformation);
  }

  virtual bool ModifiesBuffer() { return true; }
};

class InsertEmptyLineTransformation : public Transformation {
 public:
  InsertEmptyLineTransformation(Direction direction) : direction_(direction) {}

  unique_ptr<Transformation> Apply(
      EditorState* editor_state, OpenBuffer* buffer) const {
    LineColumn position = direction_ == BACKWARDS
        ? LineColumn(buffer->position().line + 1)
        : LineColumn(buffer->position().line);
    return ComposeTransformation(
        TransformationAtPosition(position,
            unique_ptr<Transformation>(new NewLineTransformation())),
        NewGotoPositionTransformation(position))
            ->Apply(editor_state, buffer);
  }

  unique_ptr<Transformation> Clone() {
    return unique_ptr<Transformation>(
        new InsertEmptyLineTransformation(direction_));
  }

  virtual bool ModifiesBuffer() { return true; }

 private:
  Direction direction_;
};

class InsertMode : public EditorMode {
 public:
  InsertMode() {}

  void ProcessInput(int c, EditorState* editor_state) {
    auto buffer = editor_state->current_buffer()->second;
    switch (c) {
      case Terminal::ESCAPE:
        buffer->MaybeAdjustPositionCol();
        buffer->Apply(editor_state, NewDeleteSuffixSuperfluousCharacters());
        buffer->PopTransformationStack();
        editor_state->PushCurrentPosition();
        editor_state->ResetStatus();
        editor_state->ResetMode();
        editor_state->ResetRepetitions();
        return;

      case Terminal::UP_ARROW:
        LOG(INFO) << "Up arrow";
        buffer->LineUp();
        return;

      case Terminal::DOWN_ARROW:
        LOG(INFO) << "Down arrow";
        buffer->LineDown();
        return;

      case Terminal::LEFT_ARROW:
        if (buffer->current_position_col() > 0) {
          buffer->set_current_position_col(buffer->current_position_col() - 1);
        }
        return;

      case Terminal::RIGHT_ARROW:
        buffer->set_current_position_col(
            min(buffer->current_position_col() + 1,
                buffer->current_line()->size()));
        return;

      case Terminal::BACKSPACE:
        {
          buffer->MaybeAdjustPositionCol();
          LineColumn start = buffer->position();
          if (buffer->at_beginning_of_line()) {
            if (buffer->at_beginning()) { return; }
            start.line--;
            start.column = buffer->contents()->at(start.line)->size();
          } else {
            start.column--;
          }
          buffer->Apply(editor_state, TransformationAtPosition(start,
              NewDeleteCharactersTransformation(1, false)));
          buffer->set_modified(true);
          editor_state->ScheduleRedraw();
        }
        return;

      case '\n':
        buffer->Apply(editor_state,
            unique_ptr<Transformation>(new NewLineTransformation()));
        buffer->set_modified(true);
        editor_state->ScheduleRedraw();
        return;
    }

    {
      shared_ptr<OpenBuffer> buffer_to_insert(
          new OpenBuffer(editor_state, "- text inserted"));
      buffer_to_insert->contents()->emplace_back(
          new Line(Line::Options(NewCopyString(string(1, c)))));
      buffer->Apply(editor_state,
          NewInsertBufferTransformation(buffer_to_insert, 1, END));
    }

    buffer->set_modified(true);
    editor_state->ScheduleRedraw();
  }
};

class RawInputTypeMode : public EditorMode {
 public:
  RawInputTypeMode() : buffering_(false) {}

  void ProcessInput(int c, EditorState* editor_state) {
    auto buffer = editor_state->current_buffer()->second;
    switch (c) {
      case Terminal::CHAR_EOF:
        line_buffer_.push_back(4);
        write(buffer->fd(), line_buffer_.c_str(), line_buffer_.size());
        line_buffer_ = "";
        break;

      case Terminal::CTRL_L:
        {
          string sequence(1, 0x0c);
          write(buffer->fd(), sequence.c_str(), sequence.size());
        }
        break;

      case Terminal::CTRL_U:
        if (!buffer) {
          line_buffer_ = "";
        } else {
          string sequence(1, 21);
          write(buffer->fd(), sequence.c_str(), sequence.size());
        }
        break;

      case Terminal::ESCAPE:
        {
          editor_state->ResetMode();
          editor_state->ResetStatus();
        }
        break;

      case Terminal::UP_ARROW:
        {
          string sequence = { 27, '[', 'A' };
          write(buffer->fd(), sequence.c_str(), sequence.size());
        }
        break;

      case Terminal::DOWN_ARROW:
        {
          string sequence = { 27, '[', 'B' };
          write(buffer->fd(), sequence.c_str(), sequence.size());
        }
        break;

      case Terminal::RIGHT_ARROW:
        {
          string sequence = { 27, '[', 'C' };
          write(buffer->fd(), sequence.c_str(), sequence.size());
        }
        break;

      case Terminal::LEFT_ARROW:
        {
          string sequence = { 27, '[', 'D' };
          write(buffer->fd(), sequence.c_str(), sequence.size());
        }
        break;

      case Terminal::BACKSPACE:
        if (buffering_) {
          if (line_buffer_.empty()) { return; }
          line_buffer_.resize(line_buffer_.size() - 1);
          auto last_line = *buffer->contents()->rbegin();
          last_line.reset(
              new Line(last_line->Substring(0, last_line->size() - 1)));
        } else {
          string contents(1, 127);
          write(buffer->fd(), contents.c_str(), contents.size());
        }
        break;

      case '\n':
        line_buffer_.push_back('\n');
        write(buffer->fd(), line_buffer_.c_str(), line_buffer_.size());
        line_buffer_ = "";
        break;

      default:
        if (buffering_) {
          buffer->AppendToLastLine(editor_state, NewCopyString(string(1, c)));
          line_buffer_.push_back(c);
          editor_state->ScheduleRedraw();
        } else {
          string contents(1, c);
          write(buffer->fd(), contents.c_str(), contents.size());
        }
    };
  }
 private:
  string line_buffer_;
  bool buffering_;
};

}  // namespace

namespace afc {
namespace editor {

using std::unique_ptr;
using std::shared_ptr;

void EnterInsertCharactersMode(EditorState* editor_state) {
  auto buffer = editor_state->current_buffer()->second;
  buffer->MaybeAdjustPositionCol();
  editor_state->SetStatus("type");
  editor_state->set_mode(unique_ptr<EditorMode>(new InsertMode()));
}

void EnterInsertMode(EditorState* editor_state) {
  editor_state->ResetStatus();

  if (!editor_state->has_current_buffer()) {
    OpenAnonymousBuffer(editor_state);
  }
  auto buffer = editor_state->current_buffer()->second;
  if (editor_state->current_buffer()->second->fd() != -1) {
    editor_state->SetStatus("type (raw)");
    editor_state->set_mode(unique_ptr<EditorMode>(new RawInputTypeMode()));
  } else if (editor_state->structure() == EditorState::CHAR) {
    editor_state->current_buffer()->second->CheckPosition();
    buffer->PushTransformationStack();
    EnterInsertCharactersMode(editor_state);
  } else if (editor_state->structure() == EditorState::LINE) {
    editor_state->current_buffer()->second->CheckPosition();
    auto buffer = editor_state->current_buffer()->second;
    buffer->PushTransformationStack();
    buffer->Apply(editor_state,
        unique_ptr<Transformation>(
            new InsertEmptyLineTransformation(editor_state->direction())));
    EnterInsertCharactersMode(editor_state);
    editor_state->ScheduleRedraw();
  }
  editor_state->ResetDirection();
  editor_state->ResetStructure();
}

}  // namespace editor
}  // namespace afc
