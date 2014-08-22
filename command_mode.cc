#include <cmath>
#include <functional>
#include <memory>
#include <map>
#include <string>

#include "advanced_mode.h"
#include "command.h"
#include "command_mode.h"
#include "file_link_mode.h"
#include "find_mode.h"
#include "help_command.h"
#include "insert_mode.h"
#include "lazy_string_append.h"
#include "line_prompt_mode.h"
#include "map_mode.h"
#include "noop_command.h"
#include "repeat_mode.h"
#include "search_handler.h"
#include "substring.h"
#include "terminal.h"

namespace {
using std::advance;
using std::ceil;
using std::make_pair;
using namespace afc::editor;

const string kPasteBuffer = "- paste buffer";

class Quit : public Command {
 public:
  const string Description() {
    return "quits";
  }

  void ProcessInput(int c, EditorState* editor_state) {
    editor_state->set_terminate(true);
  }
};

class GotoCommand : public Command {
 public:
  const string Description() {
    return "goes to Rth structure from the beginning";
  }

  void ProcessInput(int c, EditorState* editor_state) {
    if (!editor_state->has_current_buffer()) { return; }
    editor_state->PushCurrentPosition();
    switch (editor_state->structure()) {
      case EditorState::CHAR:
        {
          shared_ptr<OpenBuffer> buffer = editor_state->current_buffer()->second;
          if (buffer->current_line() == nullptr) { return; }
          size_t position =
              ComputePosition(editor_state, buffer->current_line()->size() + 1);
          assert(position <= buffer->current_line()->size());
          buffer->set_current_position_col(position);
        }
        break;

      case EditorState::WORD:
        // TODO: Implement.
        break;

      case EditorState::LINE:
        {
          shared_ptr<OpenBuffer> buffer = editor_state->current_buffer()->second;
          size_t position =
              ComputePosition(editor_state, buffer->contents()->size() + 1);
          assert(position <= buffer->contents()->size());
          buffer->set_current_position_line(position);
        }
        break;

      case EditorState::PAGE:
        {
          shared_ptr<OpenBuffer> buffer = editor_state->current_buffer()->second;
          if (buffer->contents()->empty()) { return; }
          size_t position = editor_state->visible_lines() * ComputePosition(
              editor_state,
              ceil(static_cast<double>(buffer->contents()->size())
                  / editor_state->visible_lines()));
          assert(position < buffer->contents()->size());
          buffer->set_current_position_line(position);
        }
        break;

      case EditorState::SEARCH:
        // TODO: Implement.
        break;

      case EditorState::BUFFER:
        {
          size_t position =
              ComputePosition(editor_state, editor_state->buffers()->size());
          assert(position < editor_state->buffers()->size());
          auto it = editor_state->buffers()->begin();
          advance(it, position);
          if (it != editor_state->current_buffer()) {
            editor_state->set_current_buffer(it);
            it->second->Enter(editor_state);
          }
        }
        break;
    }
    editor_state->ScheduleRedraw();
    editor_state->ResetStructure();
    editor_state->ResetDirection();
    editor_state->ResetRepetitions();
  }

 private:
  size_t ComputePosition(EditorState* editor_state, size_t elements) {
    size_t repetitions = editor_state->repetitions();
    repetitions = min(elements, max(1ul, repetitions));
    if (editor_state->direction() == FORWARDS) {
      return repetitions - 1;
    } else {
      return elements - repetitions;
    }
  }
};

class DeleteFromBuffer : public Transformation {
 public:
  DeleteFromBuffer(size_t start_line, size_t start_column, size_t end_line,
                   size_t end_column);
  unique_ptr<Transformation> Apply(
      EditorState* editor_state, OpenBuffer* buffer) const;
 private:
  size_t start_line_;
  size_t start_column_;
  size_t end_line_;
  size_t end_column_;
};

class InsertBuffer : public Transformation {
 public:
  InsertBuffer(shared_ptr<OpenBuffer> buffer_to_insert, size_t line,
               size_t column)
      : buffer_to_insert_(buffer_to_insert), line_(line), column_(column) {
    assert(buffer_to_insert_ != nullptr);
  }

  unique_ptr<Transformation> Apply(
      EditorState* editor_state, OpenBuffer* buffer) const {
    size_t size_last_line =
        (buffer_to_insert_->contents()->size() > 1 ? 0 : column_)
        + (buffer_to_insert_->contents()->size() == 0
           ? 0 : (*buffer_to_insert_->contents()->rbegin())->contents->size());
    buffer->InsertInPosition(*buffer_to_insert_->contents(), line_, column_);
    editor_state->ScheduleRedraw();
    return unique_ptr<Transformation>(new DeleteFromBuffer(
        line_, column_, line_ + buffer_to_insert_->contents()->size(), size_last_line));
  }
 private:
  shared_ptr<OpenBuffer> buffer_to_insert_;
  size_t line_;
  size_t column_;
};

// A transformation that, when applied, removes the text from the start position
// to the end position (leaving the characters immediately at the end position).
DeleteFromBuffer::DeleteFromBuffer(
    size_t start_line, size_t start_column, size_t end_line, size_t end_column)
    : start_line_(start_line),
      start_column_(start_column),
      end_line_(end_line),
      end_column_(end_column) {}

void InsertDeletedTextBuffer(EditorState* editor_state,
                             const shared_ptr<OpenBuffer>& buffer) {
  auto insert_result = editor_state->buffers()->insert(
      make_pair(kPasteBuffer, buffer));
  if (!insert_result.second) {
    insert_result.first->second = buffer;
  }
}

unique_ptr<Transformation> DeleteFromBuffer::Apply(
    EditorState* editor_state, OpenBuffer* buffer) const {
  shared_ptr<OpenBuffer> deleted_text(new OpenBuffer(kPasteBuffer));
  size_t actual_end = min(end_line_, buffer->contents()->size() - 1);
  for (size_t line = start_line_; line <= actual_end; line++) {
    auto current_line = buffer->contents()->at(line);
    size_t current_start_column = line == start_line_ ? start_column_ : 0;
    size_t current_end_column =
        line == end_line_ ? end_column_ : current_line->size();
    deleted_text->contents()->push_back(
        shared_ptr<Line>(new Line(
            Substring(current_line->contents, current_start_column,
                      current_end_column - current_start_column))));
    if (current_line->activate != nullptr) {
      current_line->activate->ProcessInput('d', editor_state);
    }
  }
  shared_ptr<LazyString> prefix = Substring(
      buffer->contents()->at(start_line_)->contents, 0, start_column_);
  shared_ptr<LazyString> contents_last_line =
      end_line_ < buffer->contents()->size()
      ? StringAppend(prefix,
                     Substring(buffer->contents()->at(end_line_)->contents,
                               end_column_))
      : prefix;
  buffer->contents()->erase(
      buffer->contents()->begin() + start_line_ + 1,
      buffer->contents()->begin() + actual_end + 1);
  buffer->contents()->at(start_line_).reset(new Line(contents_last_line));
  buffer->CheckPosition();
  assert(deleted_text != nullptr);
  editor_state->ScheduleRedraw();

  InsertDeletedTextBuffer(editor_state, deleted_text);

  return unique_ptr<Transformation>(
      new InsertBuffer(deleted_text, start_line_, start_column_));
}

class Delete : public Command {
 public:
  const string Description() {
    return "deletes the current item (char, word, line ...)";
  }

  void ProcessInput(int c, EditorState* editor_state) {
    if (editor_state->buffers()->empty()) { return; }
    shared_ptr<OpenBuffer> buffer = editor_state->current_buffer()->second;

    switch (editor_state->structure()) {
      case EditorState::CHAR:
        if (!editor_state->has_current_buffer()) { return; }
        DeleteCharacters(editor_state);
        break;

      case EditorState::WORD:
        // TODO: Implement.
        editor_state->SetStatus("Oops, delete word is not yet implemented.");
        break;

      case EditorState::LINE:
        if (!editor_state->has_current_buffer()) { return; }
        {
          auto buffer = editor_state->current_buffer()->second;
          DeleteFromBuffer deleter(
              buffer->current_position_line(), 0,
              buffer->current_position_line() + editor_state->repetitions(), 0);
          editor_state->ApplyToCurrentBuffer(deleter);
        }
        break;

      case EditorState::PAGE:
        // TODO: Implement.
        editor_state->SetStatus("Oops, delete page is not yet implemented.");
        break;

      case EditorState::SEARCH:
        // TODO: Implement.
        editor_state->SetStatus("Ooops, delete search is not yet implemented.");
        break;

      case EditorState::BUFFER:
        auto buffer_to_erase = editor_state->current_buffer();
        if (editor_state->current_buffer() == editor_state->buffers()->begin()) {
          editor_state->set_current_buffer(editor_state->buffers()->end());
        }
        auto it = editor_state->current_buffer();
        --it;
        editor_state->set_current_buffer(it);
        editor_state->buffers()->erase(buffer_to_erase);
        if (editor_state->current_buffer() == buffer_to_erase) {
          editor_state->set_current_buffer(editor_state->buffers()->end());
        } else {
          editor_state->current_buffer()->second->Enter(editor_state);
        }
    }

    editor_state->ResetStructure();
    editor_state->ResetRepetitions();
  }

 private:
  void DeleteCharacters(EditorState* editor_state) {
    shared_ptr<OpenBuffer> buffer = editor_state->current_buffer()->second;
    if (buffer->current_line() == nullptr) { return; }
    buffer->MaybeAdjustPositionCol();

    size_t end_line = buffer->current_position_line();
    size_t end_column = buffer->current_position_col();
    auto current_line = buffer->contents()->begin() + end_line;
    while (editor_state->repetitions() > 0) {
      if (current_line == buffer->contents()->end()) {
        editor_state->set_repetitions(0);
        continue;
      }
      size_t characters_left = (*current_line)->contents->size() - end_column;
      if (editor_state->repetitions() <= characters_left) {
        end_column += editor_state->repetitions();
        editor_state->set_repetitions(0);
        continue;
      }
 
      editor_state->set_repetitions(
          editor_state->repetitions() - characters_left - 1);
      end_line ++;
      end_column = 0;
    }

    DeleteFromBuffer deleter(
        buffer->current_position_line(), buffer->current_position_col(),
        end_line, end_column);
    editor_state->ApplyToCurrentBuffer(deleter);
  }
};

// TODO: Replace with insert.  Insert should be called 'type'.
class Paste : public Command {
 public:
  const string Description() {
    return "pastes the last deleted text";
  }

  void ProcessInput(int c, EditorState* editor_state) {
    if (!editor_state->has_current_buffer()) { return; }
    auto it = editor_state->buffers()->find(kPasteBuffer);
    if (it == editor_state->buffers()->end()) {
      editor_state->SetStatus("No text to paste.");
      return;
    }
    if (it == editor_state->current_buffer()) {
      editor_state->SetStatus("You shall not paste into the paste buffer.");
      return;
    }
    editor_state->current_buffer()->second->InsertInCurrentPosition(
        *it->second->contents());
    editor_state->ScheduleRedraw();
  }
};

class UndoCommand : public Command {
 public:
  const string Description() {
    return "undoes the last change to the current buffer";
  }

  void ProcessInput(int c, EditorState* editor_state) {
    if (!editor_state->has_current_buffer()) { return; }
    editor_state->current_buffer()->second->Undo(editor_state);
  }
};

class GotoPreviousPositionCommand : public Command {
 public:
  const string Description() {
    return "go back to previous position";
  }

  void ProcessInput(int c, EditorState* editor_state) {
    Go(editor_state);
    editor_state->ResetDirection();
    editor_state->ResetRepetitions();
    editor_state->ResetStructure();
  }

  static void Go(EditorState* editor_state) {
    if (!editor_state->HasPositionsInStack()) {
      return;
    }
    while (editor_state->repetitions() > 0) {
      if (editor_state->direction() == BACKWARDS
          && !editor_state->MovePositionsStack(BACKWARDS)) {
        return;
      }
      const Position pos = editor_state->ReadPositionsStack();
      auto it = editor_state->buffers()->find(pos.buffer);
      if (it != editor_state->buffers()->end()
          && (pos.buffer != editor_state->current_buffer()->first
              || (editor_state->structure() <= EditorState::LINE
                  && pos.line != editor_state->current_buffer()->second->current_position_line())
              || (editor_state->structure() <= EditorState::CHAR
                  && pos.col != editor_state->current_buffer()->second->current_position_col()))) {
        editor_state->set_current_buffer(it);
        it->second->set_current_position_line(pos.line);
        it->second->set_current_position_col(pos.col);
        it->second->Enter(editor_state);
        editor_state->ScheduleRedraw();
        editor_state->set_repetitions(editor_state->repetitions() - 1);
      }
      if (editor_state->direction() == FORWARDS
          && !editor_state->MovePositionsStack(FORWARDS)) {
        return;
      }
    }
  }
};

class GotoNextPositionCommand : public Command {
 public:
  const string Description() {
    return "go forwards to next position";
  }

  void ProcessInput(int c, EditorState* editor_state) {
    editor_state->set_direction( 
        ReverseDirection(editor_state->direction()));
    GotoPreviousPositionCommand::Go(editor_state);
    editor_state->ResetDirection();
    editor_state->ResetRepetitions();
    editor_state->ResetStructure();
  }
};

class LineUp : public Command {
 public:
  const string Description();
  static void Move(int c, EditorState* editor_state);
  void ProcessInput(int c, EditorState* editor_state);
};

class LineDown : public Command {
 public:
  const string Description();
  static void Move(int c, EditorState* editor_state);
  void ProcessInput(int c, EditorState* editor_state);
};

class PageUp : public Command {
 public:
  const string Description();
  static void Move(int c, EditorState* editor_state);
  void ProcessInput(int c, EditorState* editor_state);
};

class PageDown : public Command {
 public:
  const string Description();
  void ProcessInput(int c, EditorState* editor_state);
};

class MoveForwards : public Command {
 public:
  const string Description();
  void ProcessInput(int c, EditorState* editor_state);
  static void Move(int c, EditorState* editor_state);
};

class MoveBackwards : public Command {
 public:
  const string Description();
  void ProcessInput(int c, EditorState* editor_state);
  static void Move(int c, EditorState* editor_state);
};

const string LineUp::Description() {
  return "moves up one line";
}

/* static */ void LineUp::Move(int c, EditorState* editor_state) {
  if (editor_state->direction() == BACKWARDS) {
    editor_state->set_direction(FORWARDS);
    LineDown::Move(c, editor_state);
    return;
  }
  if (!editor_state->has_current_buffer()) { return; }
  shared_ptr<OpenBuffer> buffer = editor_state->current_buffer()->second;
  switch (editor_state->structure()) {
    case EditorState::CHAR:
      {
        if (editor_state->repetitions() > 1) {
          // Saving on single-lines changes makes this very verbose, lets avoid that.
          editor_state->PushCurrentPosition();
        }
        size_t pos = buffer->current_position_line();
        if (editor_state->repetitions() < pos) {
          buffer->set_current_position_line(pos - editor_state->repetitions());
        } else {
          buffer->set_current_position_line(0);
        }
      }
      break;

    case EditorState::WORD:
      // Move in whole pages.
      editor_state->set_repetitions(
          editor_state->repetitions() * editor_state->visible_lines());
      editor_state->set_structure(EditorState::CHAR);
      Move(c, editor_state);
      break;

    default:
      editor_state->MoveBufferBackwards(editor_state->repetitions());
      editor_state->ScheduleRedraw();
  }
  editor_state->ResetStructure();
  editor_state->ResetRepetitions();
  editor_state->ResetDirection();
}

void LineUp::ProcessInput(int c, EditorState* editor_state) {
  Move(c, editor_state);
}

const string LineDown::Description() {
  return "moves down one line";
}

/* static */ void LineDown::Move(int c, EditorState* editor_state) {
  if (editor_state->direction() == BACKWARDS) {
    editor_state->set_direction(FORWARDS);
    LineUp::Move(c, editor_state);
    return;
  }
  if (!editor_state->has_current_buffer()) { return; }
  switch (editor_state->structure()) {
    case EditorState::CHAR:
      {
        shared_ptr<OpenBuffer> buffer = editor_state->current_buffer()->second;
        if (editor_state->repetitions() > 1) {
          // Saving on single-lines changes makes this very verbose, lets avoid that.
          editor_state->PushCurrentPosition();
        }
        size_t pos = buffer->current_position_line();
        buffer->set_current_position_line(min(pos + editor_state->repetitions(),
                                              buffer->contents()->size()));
      }
      break;

    case EditorState::WORD:
      // Move in whole pages.
      editor_state->set_repetitions(
          editor_state->repetitions() * editor_state->visible_lines());
      editor_state->set_structure(EditorState::CHAR);
      Move(c, editor_state);
      break;

    default:
      editor_state->MoveBufferForwards(editor_state->repetitions());
      editor_state->ScheduleRedraw();
  }
  editor_state->ResetStructure();
  editor_state->ResetRepetitions();
  editor_state->ResetDirection();
}

void LineDown::ProcessInput(int c, EditorState* editor_state) {
  Move(c, editor_state);
}

const string PageUp::Description() {
  return "moves up one page";
}

void PageUp::ProcessInput(int c, EditorState* editor_state) {
  editor_state->set_repetitions(
      editor_state->repetitions() * editor_state->visible_lines());
  editor_state->ResetStructure();
  LineUp::Move(c, editor_state);
}

const string PageDown::Description() {
  return "moves down one page";
}

void PageDown::ProcessInput(int c, EditorState* editor_state) {
  editor_state->set_repetitions(
      editor_state->repetitions() * editor_state->visible_lines());
  editor_state->ResetStructure();
  LineDown::Move(c, editor_state);
}

const string MoveForwards::Description() {
  return "moves forwards";
}

void MoveForwards::ProcessInput(int c, EditorState* editor_state) {
  Move(c, editor_state);
}

/* static */ void MoveForwards::Move(int c, EditorState* editor_state) {
  if (editor_state->direction() == BACKWARDS) {
    editor_state->set_direction(FORWARDS);
    MoveBackwards::Move(c, editor_state);
    return;
  }
  switch (editor_state->structure()) {
    case EditorState::CHAR:
      {
        if (!editor_state->has_current_buffer()) { return; }
        shared_ptr<OpenBuffer> buffer = editor_state->current_buffer()->second;
        if (buffer->current_line() == nullptr) { return; }
        if (editor_state->repetitions() > 1) {
          editor_state->PushCurrentPosition();
        }
        buffer->set_current_position_col(min(
            buffer->current_position_col() + editor_state->repetitions(),
            buffer->current_line()->size()));

        editor_state->ResetRepetitions();
        editor_state->ResetStructure();
        editor_state->ResetDirection();
      }
      break;

    case EditorState::WORD:
      {
        if (!editor_state->has_current_buffer()) { return; }
        shared_ptr<OpenBuffer> buffer = editor_state->current_buffer()->second;
        if (buffer->current_line() == nullptr) { return; }
        buffer->CheckPosition();
        buffer->MaybeAdjustPositionCol();
        editor_state->PushCurrentPosition();
        const string& word_characters =
            buffer->read_string_variable(buffer->variable_word_characters());
        while (editor_state->repetitions() > 0) {
          // Seek forwards until we're not in a word character.
          while (buffer->current_position_col() < buffer->current_line()->size()
                 && word_characters.find(buffer->current_character()) != string::npos) {
            buffer->set_current_position_col(buffer->current_position_col() + 1);
          }

          // Seek forwards until we're in a word character.
          bool advanced = false;
          while (!buffer->at_end()
                 && (buffer->current_position_col() == buffer->current_line()->contents->size()
                     || word_characters.find(buffer->current_character()) == string::npos)) {
            if (buffer->current_position_col() == buffer->current_line()->contents->size()) {
              buffer->set_current_position_line(buffer->current_position_line() + 1);
              buffer->set_current_position_col(0);
            } else {
              buffer->set_current_position_col(buffer->current_position_col() + 1);
            }
            advanced = true;
          }
          if (advanced) {
            editor_state->set_repetitions(editor_state->repetitions() - 1);
          } else {
            editor_state->set_repetitions(0);
          }
        }
        editor_state->ResetRepetitions();
        editor_state->ResetStructure();
        editor_state->ResetDirection();
      }
      break;

    case EditorState::SEARCH:
      SearchHandler(editor_state->last_search_query(), editor_state);
      editor_state->ResetStructure();
      break;

    default:
      editor_state->set_structure(
          EditorState::LowerStructure(
              EditorState::LowerStructure(editor_state->structure())));
      LineDown::Move(c, editor_state);
  }
}

const string MoveBackwards::Description() {
  return "moves backwards";
}

void MoveBackwards::ProcessInput(int c, EditorState* editor_state) {
  Move(c, editor_state);
}

/* static */ void MoveBackwards::Move(int c, EditorState* editor_state) {
  if (editor_state->direction() == BACKWARDS) {
    editor_state->set_direction(FORWARDS);
    MoveForwards::Move(c, editor_state);
    return;
  }
  switch (editor_state->structure()) {
    case EditorState::CHAR:
      {
        if (!editor_state->has_current_buffer()) { return; }
        shared_ptr<OpenBuffer> buffer = editor_state->current_buffer()->second;
        if (buffer->current_line() == nullptr) { return; }
        if (editor_state->repetitions() > 1) {
          editor_state->PushCurrentPosition();
        }
        if (buffer->current_position_col() > buffer->current_line()->size()) {
          buffer->set_current_position_col(buffer->current_line()->size());
        }
        if (buffer->current_position_col() > editor_state->repetitions()) {
          buffer->set_current_position_col(
              buffer->current_position_col() - editor_state->repetitions());
        } else {
          buffer->set_current_position_col(0);
        }

        editor_state->ResetRepetitions();
        editor_state->ResetStructure();
        editor_state->ResetDirection();
      }
      break;

    case EditorState::WORD:
      {
        if (!editor_state->has_current_buffer()) { return; }
        shared_ptr<OpenBuffer> buffer = editor_state->current_buffer()->second;
        if (buffer->current_line() == nullptr) { return; }
        buffer->CheckPosition();
        buffer->MaybeAdjustPositionCol();
        editor_state->PushCurrentPosition();
        const string& word_characters =
            buffer->read_string_variable(buffer->variable_word_characters());
        while (editor_state->repetitions() > 0) {
          // Seek backwards until we're not after a word character.
          while (buffer->current_position_col() > 0
                 && word_characters.find(buffer->previous_character()) != string::npos) {
            buffer->set_current_position_col(buffer->current_position_col() - 1);
          }

          // Seek backwards until we're just after a word character.
          bool advanced = false;
          while (!buffer->at_beginning()
                 && (buffer->at_beginning_of_line()
                     || word_characters.find(buffer->previous_character()) == string::npos)) {
            if (buffer->at_beginning_of_line()) {
              buffer->set_current_position_line(buffer->current_position_line() - 1);
              buffer->set_current_position_col(buffer->current_line()->contents->size());
            } else {
              buffer->set_current_position_col(buffer->current_position_col() - 1);
            }
            advanced = true;
          }
          if (advanced) {
            editor_state->set_repetitions(editor_state->repetitions() - 1);
          } else {
            editor_state->set_repetitions(0);
          }
        }
        if (!buffer->at_beginning_of_line()) {
          buffer->set_current_position_col(buffer->current_position_col() - 1);
        }

        editor_state->ResetRepetitions();
        editor_state->ResetStructure();
        editor_state->ResetDirection();
      }
      break;

    case EditorState::SEARCH:
      editor_state->set_direction(BACKWARDS);
      SearchHandler(editor_state->last_search_query(), editor_state);
      editor_state->ResetStructure();
      break;

    default:
      editor_state->set_structure(
          EditorState::LowerStructure(
              EditorState::LowerStructure(editor_state->structure())));
      LineUp::Move(c, editor_state);
  }
}

class EnterInsertMode : public Command {
 public:
  const string Description() {
    return "enters insert mode";
  }

  void ProcessInput(int c, EditorState* editor_state) {
    afc::editor::EnterInsertMode(editor_state);
  }
};

class EnterAdvancedMode : public Command {
 public:
  const string Description() {
    return "enters advanced-command mode (press 'a?' for more)";
  }

  void ProcessInput(int c, EditorState* editor_state) {
    editor_state->set_mode(NewAdvancedMode());
  }
};

class EnterFindMode : public Command {
 public:
  const string Description() {
    return "finds occurrences of a character";
  }

  void ProcessInput(int c, EditorState* editor_state) {
    editor_state->set_mode(NewFindMode());
  }
};

class ReverseDirectionCommand : public Command {
 public:
  const string Description() {
    return "reverses the direction of the next command";
  }

  void ProcessInput(int c, EditorState* editor_state) {
    Direction previous_direction = editor_state->direction();
    editor_state->set_default_direction(FORWARDS);
    editor_state->set_direction(ReverseDirection(previous_direction));
  }
};

class ReverseDefaultDirectionCommand : public Command {
 public:
  const string Description() {
    return "reverses the direction of future commands";
  }

  void ProcessInput(int c, EditorState* editor_state) {
    editor_state->set_default_direction(
        ReverseDirection(editor_state->default_direction()));
  }
};

void SetRepetitions(EditorState* editor_state, int number) {
  editor_state->set_repetitions(number);
}

class StructureMode : public EditorMode {
 public:
  void ProcessInput(int c, EditorState* editor_state) {
    editor_state->set_mode(NewCommandMode());
    switch (c) {
      case 'c':
        editor_state->set_default_structure(EditorState::CHAR);
        editor_state->set_structure(EditorState::CHAR);
        break;
      case 'w':
        editor_state->set_default_structure(EditorState::CHAR);
        editor_state->set_structure(EditorState::WORD);
        break;
      case 'l':
        editor_state->set_default_structure(EditorState::CHAR);
        editor_state->set_structure(EditorState::LINE);
        break;
      case 'p':
        editor_state->set_default_structure(EditorState::CHAR);
        editor_state->set_structure(EditorState::PAGE);
        break;
      case 's':
        editor_state->set_default_structure(EditorState::CHAR);
        editor_state->set_structure(EditorState::SEARCH);
        break;
      case 'b':
        editor_state->set_default_structure(EditorState::CHAR);
        editor_state->set_structure(EditorState::BUFFER);
        break;
      case 'C':
        editor_state->set_default_structure(EditorState::CHAR);
        break;
      case 'W':
        editor_state->set_default_structure(EditorState::WORD);
        break;
      case 'L':
        editor_state->set_default_structure(EditorState::LINE);
        break;
      case 'S':
        editor_state->set_default_structure(EditorState::SEARCH);
        break;
      case 'P':
        editor_state->set_default_structure(EditorState::PAGE);
        break;
      case 'B':
        editor_state->set_default_structure(EditorState::BUFFER);
        break;
      case Terminal::ESCAPE:
        editor_state->set_default_structure(EditorState::CHAR);
        editor_state->set_structure(EditorState::CHAR);
        break;
      default:
        editor_state->mode()->ProcessInput(c, editor_state);
    }
  }
};

class EnterStructureMode : public Command {
  const string Description() {
    return "sets the structure affected by commands";
  }

  void ProcessInput(int c, EditorState* editor_state) {
    editor_state->set_mode(unique_ptr<EditorMode>(new StructureMode()));
  }
};

class NumberMode : public Command {
 public:
  NumberMode(function<void(EditorState*, int)> consumer)
      : description_(""), consumer_(consumer) {}

  NumberMode(
      const string& description, function<void(EditorState*, int)> consumer)
      : description_(description), consumer_(consumer) {}

  const string Description() {
    return description_;
  }

  void ProcessInput(int c, EditorState* editor_state) {
    editor_state->set_mode(NewRepeatMode(consumer_));
    if (c < '0' || c > '9') { return; }
    editor_state->mode()->ProcessInput(c, editor_state);
  }

 private:
  const string description_;
  function<void(EditorState*, int)> consumer_;
};

class ActivateLink : public Command {
 public:
  const string Description() {
    return "activates the current link (if any)";
  }

  void ProcessInput(int c, EditorState* editor_state) {
    if (!editor_state->has_current_buffer()) { return; }
    shared_ptr<OpenBuffer> buffer = editor_state->current_buffer()->second;
    if (buffer->current_line() == nullptr) { return; }
    if (buffer->current_line()->activate.get() != nullptr) {
      buffer->current_line()->activate->ProcessInput(c, editor_state);
    } else {
      buffer->MaybeAdjustPositionCol();
      string line = buffer->current_line()->contents->ToString();

      const string& path_characters =
          buffer->read_string_variable(buffer->variable_path_characters());

      size_t start = line.find_last_not_of(
          path_characters, buffer->current_position_col());
      if (start != line.npos) {
        line = line.substr(start + 1);
      }

      size_t end = line.find_first_not_of(path_characters);
      if (end != line.npos) {
        line = line.substr(0, end);
      }

      unique_ptr<EditorMode> mode =
          NewFileLinkMode(editor_state, line, 0, true);
      if (mode.get() != nullptr) {
        mode->ProcessInput(c, editor_state);
      }
    }
  }
};

static const map<int, Command*>& GetCommandModeMap() {
  static map<int, Command*> output;
  if (output.empty()) {
    output.insert(make_pair('q', new Quit()));

    output.insert(make_pair('a', new EnterAdvancedMode()));
    output.insert(make_pair('i', new EnterInsertMode()));
    output.insert(make_pair('f', new EnterFindMode()));
    output.insert(make_pair('r', new ReverseDirectionCommand()));
    output.insert(make_pair('R', new ReverseDefaultDirectionCommand()));

    output.insert(make_pair(
        '/',
        NewLinePromptCommand("/", "searches for a string", SearchHandler).release()));

    output.insert(make_pair('g', new GotoCommand()));

    output.insert(make_pair('d', new Delete()));
    output.insert(make_pair('p', new Paste()));
    output.insert(make_pair('u', new UndoCommand()));
    output.insert(make_pair('\n', new ActivateLink()));

    output.insert(make_pair('b', new GotoPreviousPositionCommand()));
    output.insert(make_pair('B', new GotoNextPositionCommand()));
    output.insert(make_pair('j', new LineDown()));
    output.insert(make_pair('k', new LineUp()));
    output.insert(make_pair('l', new MoveForwards()));
    output.insert(make_pair('h', new MoveBackwards()));

    output.insert(make_pair('s', new EnterStructureMode()));

    output.insert(make_pair('?', NewHelpCommand(output, "command mode").release()));

    output.insert(make_pair('0', new NumberMode(SetRepetitions)));
    output.insert(make_pair('1', new NumberMode(SetRepetitions)));
    output.insert(make_pair('2', new NumberMode(SetRepetitions)));
    output.insert(make_pair('3', new NumberMode(SetRepetitions)));
    output.insert(make_pair('4', new NumberMode(SetRepetitions)));
    output.insert(make_pair('5', new NumberMode(SetRepetitions)));
    output.insert(make_pair('6', new NumberMode(SetRepetitions)));
    output.insert(make_pair('7', new NumberMode(SetRepetitions)));
    output.insert(make_pair('8', new NumberMode(SetRepetitions)));
    output.insert(make_pair('9', new NumberMode(SetRepetitions)));
    output.insert(make_pair(Terminal::DOWN_ARROW, new LineDown()));
    output.insert(make_pair(Terminal::UP_ARROW, new LineUp()));
    output.insert(make_pair(Terminal::LEFT_ARROW, new MoveBackwards()));
    output.insert(make_pair(Terminal::RIGHT_ARROW, new MoveForwards()));
    output.insert(make_pair(Terminal::PAGE_DOWN, new PageDown()));
    output.insert(make_pair(Terminal::PAGE_UP, new PageUp()));
  }
  return output;
}

}  // namespace

namespace afc {
namespace editor {

using std::map;
using std::unique_ptr;

unique_ptr<EditorMode> NewCommandMode() {
  unique_ptr<MapMode> mode(new MapMode(GetCommandModeMap(), NoopCommand()));
  return std::move(mode);
}

}  // namespace afc
}  // namespace editor
