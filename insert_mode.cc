#include "insert_mode.h"

#include <cassert>
#include <memory>

extern "C" {
#include <unistd.h>
}

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

void DeleteSuffixSuperfluousCharacters(
    EditorState* editor_state, OpenBuffer* buffer) {
  const string& superfluous_characters(buffer->read_string_variable(
      OpenBuffer::variable_line_suffix_superfluous_characters()));
  const auto line = buffer->current_line();
  if (!line) { return; }
  size_t pos = line->contents->size();
  while (pos > 0
         && superfluous_characters.find(line->contents->get(pos - 1)) != string::npos) {
    pos--;
  }
  if (pos == line->contents->size()) {
    return;
  }
  int line_count = buffer->position().line;
  buffer->Apply(editor_state,
       *NewDeleteTransformation(LineColumn(line_count, pos),
                                LineColumn(line_count, line->contents->size()),
                                false));
}

class InsertMode : public EditorMode {
 public:
  InsertMode() {}

  void ProcessInput(int c, EditorState* editor_state) {
    auto buffer = editor_state->current_buffer()->second;
    buffer->MaybeAdjustPositionCol();
    switch (c) {
      case Terminal::ESCAPE:
        DeleteSuffixSuperfluousCharacters(editor_state, buffer.get());
        editor_state->PushCurrentPosition();
        editor_state->ResetStatus();
        editor_state->ResetMode();
        editor_state->ResetRepetitions();
        return;

      case Terminal::BACKSPACE:
        {
          LineColumn start = buffer->position();
          if (buffer->at_beginning_of_line()) {
            if (buffer->at_beginning()) { return; }
            start.line--;
            start.column = buffer->contents()->at(start.line)->contents->size();
          } else {
            start.column--;
          }
          buffer->Apply(editor_state,
              *NewDeleteTransformation(start, buffer->position(), false).get());
          buffer->set_modified(true);
          editor_state->ScheduleRedraw();
        }
        return;

      case '\n':
        auto position = buffer->position();
        size_t pos = position.column;
        auto current_line = buffer->current_line()->contents;

        if (buffer->read_bool_variable(OpenBuffer::variable_atomic_lines())
            && pos != 0
            && pos != current_line->size()) {
          return;
        }

        const string& line_prefix_characters(buffer->read_string_variable(
            OpenBuffer::variable_line_prefix_characters()));
        size_t prefix_end = 0;
        if (!buffer->read_bool_variable(OpenBuffer::variable_paste_mode())) {
          while (prefix_end < pos
                 && (line_prefix_characters.find(current_line->get(prefix_end))
                     != line_prefix_characters.npos)) {
            prefix_end++;
          }
        }

        shared_ptr<LazyString> continuation(
            StringAppend(
                Substring(current_line, 0, prefix_end),
                Substring(current_line, position.column,
                          current_line->size() - position.column)));

        TransformationStack transformation;

        transformation.PushBack(NewDeleteTransformation(
            position, LineColumn(position.line, current_line->size()), false));

        {
          shared_ptr<OpenBuffer> buffer_to_insert(
            new OpenBuffer(editor_state, "- text inserted"));
          buffer_to_insert->contents()->emplace_back(new Line(EmptyString()));
          buffer_to_insert->contents()->emplace_back(new Line(continuation));
          transformation.PushBack(NewInsertBufferTransformation(
              buffer_to_insert, buffer->position(), 1));
        }

        buffer->Apply(editor_state, transformation);
        buffer->set_position(LineColumn(position.line + 1, prefix_end));
        buffer->set_modified(true);
        editor_state->ScheduleRedraw();
        return;
    }

    {
      shared_ptr<OpenBuffer> buffer_to_insert(
          new OpenBuffer(editor_state, "- text inserted"));
      buffer_to_insert->contents()->emplace_back(
          new Line(NewCopyString(string(1, c))));
      buffer->Apply(editor_state,
          *NewInsertBufferTransformation(
              buffer_to_insert, buffer->position(), 1).get());
    }

    buffer->set_modified(true);
    editor_state->ScheduleRedraw();
  }
};

class RawInputTypeMode : public EditorMode {
  void ProcessInput(int c, EditorState* editor_state) {
    switch (c) {
      case Terminal::ESCAPE:
        editor_state->ResetMode();
        editor_state->ResetStatus();
        break;
      default:
        string str(1, static_cast<char>(c));
        write(editor_state->current_buffer()->second->fd(), str.c_str(), 1);
    };
  }
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
  if (editor_state->current_buffer()->second->fd() != -1) {
    editor_state->SetStatus("type (raw)");
    editor_state->set_mode(unique_ptr<EditorMode>(new RawInputTypeMode()));
  } else if (editor_state->structure() == EditorState::CHAR) {
    editor_state->current_buffer()->second->CheckPosition();
    EnterInsertCharactersMode(editor_state);
  } else if (editor_state->structure() == EditorState::LINE) {
    editor_state->current_buffer()->second->CheckPosition();
    auto buffer = editor_state->current_buffer()->second;
    shared_ptr<Line> line(new Line());
    line->contents = EmptyString();
    if (editor_state->direction() == BACKWARDS) {
      buffer->set_current_position_line(buffer->current_position_line() + 1);
    }
    buffer->contents()->insert(
        buffer->contents()->begin() + buffer->current_position_line(),
        line);
    EnterInsertCharactersMode(editor_state);
    editor_state->ScheduleRedraw();
  }
  editor_state->ResetDirection();
  editor_state->ResetStructure();
}

}  // namespace editor
}  // namespace afc
