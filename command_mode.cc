#include <cmath>
#include <functional>
#include <fstream>
#include <iostream>
#include <memory>
#include <map>
#include <string>

#include "advanced_mode.h"
#include "char_buffer.h"
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
#include "transformation.h"

namespace {
using std::advance;
using std::ceil;
using std::make_pair;
using namespace afc::editor;

class GotoCommand : public Command {
 public:
  const string Description() {
    return "goes to Rth structure from the beginning";
  }

  void ProcessInput(int c, EditorState* editor_state) {
    if (!editor_state->has_current_buffer()) { return; }
    switch (editor_state->structure()) {
      case EditorState::CHAR:
        {
          shared_ptr<OpenBuffer> buffer = editor_state->current_buffer()->second;
          if (buffer->current_line() == nullptr) { return; }
          const string line_prefix_characters = buffer->read_string_variable(
              OpenBuffer::variable_line_prefix_characters());
          const auto& line = buffer->current_line();
          size_t start = 0;
          while (start < line->size()
                 && (line_prefix_characters.find(line->contents->get(start))
                     != string::npos)) {
            start++;
          }
          size_t position = ComputePosition(
              editor_state, line->size() + 1 - start);
          assert(start + position <= line->size());
          buffer->set_current_position_col(start + position);
        }
        break;

      case EditorState::WORD:
        {
          // TODO: Handle reverse direction
          shared_ptr<OpenBuffer> buffer = editor_state->current_buffer()->second;
          LineColumn position(buffer->position().line);
          while (editor_state->repetitions() > 0) {
            LineColumn start, end;
            if (!buffer->BoundWordAt(position, &start, &end)) {
              editor_state->set_repetitions(0);
              continue;
            }
            editor_state->set_repetitions(editor_state->repetitions() - 1);
            if (editor_state->repetitions() == 0) {
              position = start;
            } else if (end.column == buffer->LineAt(position.line)->contents->size()) {
              position = LineColumn(end.line + 1);
            } else {
              position = LineColumn(end.line, end.column + 1);
            }
          }
          buffer->set_position(position);
        }
        break;

      case EditorState::LINE:
        {
          shared_ptr<OpenBuffer> buffer = editor_state->current_buffer()->second;
          size_t position =
              ComputePosition(editor_state, buffer->contents()->size());
          assert(position <= buffer->contents()->size());
          buffer->set_current_position_line(position);
        }
        break;

      case EditorState::PAGE:
        {
          shared_ptr<OpenBuffer> buffer = editor_state->current_buffer()->second;
          assert(!buffer->contents()->empty());
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
    editor_state->PushCurrentPosition();
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
        if (!editor_state->has_current_buffer()) { return; }
        {
          // TODO: Honor repetition.
          auto buffer = editor_state->current_buffer()->second;
          LineColumn start, end;
          if (!buffer->BoundWordAt(buffer->position(), &start, &end)) {
            return;
          }
          assert(start.line == end.line);
          assert(start.column + 1 < end.column);
          unique_ptr<Transformation> transformation =
              NewDeleteTransformation(start, end);
          editor_state->ApplyToCurrentBuffer(*transformation);
        }
        break;

      case EditorState::LINE:
        if (!editor_state->has_current_buffer()) { return; }
        {
          size_t line = editor_state->current_buffer()->second->position().line;
          unique_ptr<Transformation> transformation = NewDeleteTransformation(
              LineColumn(line, 0),
              LineColumn(line + editor_state->repetitions(), 0));
          editor_state->ApplyToCurrentBuffer(*transformation);
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

    LineColumn end = buffer->position();
    auto current_line = buffer->contents()->begin() + end.line;
    while (editor_state->repetitions() > 0) {
      if (current_line == buffer->contents()->end()) {
        editor_state->set_repetitions(0);
        continue;
      }
      size_t characters_left = (*current_line)->contents->size() - end.column;
      if (editor_state->repetitions() <= characters_left
          || end.line + 1 == buffer->contents()->size()) {
        end.column += min(characters_left, editor_state->repetitions());
        editor_state->set_repetitions(0);
        continue;
      }

      editor_state->set_repetitions(
          editor_state->repetitions() - characters_left - 1);
      end.line ++;
      end.column = 0;
    }

    unique_ptr<Transformation> transformation(
        NewDeleteTransformation(buffer->position(), end));
    editor_state->ApplyToCurrentBuffer(*transformation);
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
    auto it = editor_state->buffers()->find(OpenBuffer::kPasteBuffer);
    if (it == editor_state->buffers()->end()) {
      editor_state->SetStatus("No text to paste.");
      return;
    }
    if (it == editor_state->current_buffer()) {
      editor_state->SetStatus("You shall not paste into the paste buffer.");
      return;
    }
    auto buffer = editor_state->current_buffer()->second;
    buffer->MaybeAdjustPositionCol();
    unique_ptr<Transformation> transformation = NewInsertBufferTransformation(
        it->second, buffer->position(), editor_state->repetitions());
    editor_state->ApplyToCurrentBuffer(*transformation);
    editor_state->ResetRepetitions();
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
    editor_state->ResetRepetitions();
    editor_state->ResetDirection();
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
      if (!editor_state->MovePositionsStack(editor_state->direction())) {
        return;
      }
      const BufferPosition pos = editor_state->ReadPositionsStack();
      auto it = editor_state->buffers()->find(pos.buffer);
      const LineColumn current_position =
          editor_state->current_buffer()->second->position();
      if (it != editor_state->buffers()->end()
          && (pos.buffer != editor_state->current_buffer()->first
              || (editor_state->structure() <= EditorState::LINE
                  && pos.position.line != current_position.line)
              || (editor_state->structure() <= EditorState::CHAR
                  && pos.position.column != current_position.column))) {
        editor_state->set_current_buffer(it);
        it->second->set_position(pos.position);
        it->second->Enter(editor_state);
        editor_state->ScheduleRedraw();
        editor_state->set_repetitions(editor_state->repetitions() - 1);
      }
    }
  }
};

class LineUp : public Command {
 public:
  const string Description();
  static void Move(int c, EditorState* editor_state,
                   EditorState::Structure structure);
  void ProcessInput(int c, EditorState* editor_state);
};

class LineDown : public Command {
 public:
  const string Description();
  static void Move(int c, EditorState* editor_state,
                   EditorState::Structure structure);
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

/* static */ void LineUp::Move(
    int c, EditorState* editor_state, EditorState::Structure structure) {
  if (editor_state->direction() == BACKWARDS) {
    editor_state->set_direction(FORWARDS);
    LineDown::Move(c, editor_state, structure);
    return;
  }
  if (!editor_state->has_current_buffer()) { return; }
  shared_ptr<OpenBuffer> buffer = editor_state->current_buffer()->second;
  switch (structure) {
    case EditorState::CHAR:
      {
        size_t pos = buffer->current_position_line();
        if (editor_state->repetitions() < pos) {
          buffer->set_current_position_line(pos - editor_state->repetitions());
        } else {
          buffer->set_current_position_line(0);
        }
        if (editor_state->repetitions() > 1) {
          // Saving on single-lines changes makes this very verbose, lets avoid that.
          editor_state->PushCurrentPosition();
        }
      }
      break;

    case EditorState::WORD:
      // Move in whole pages.
      editor_state->set_repetitions(
          editor_state->repetitions() * editor_state->visible_lines());
      Move(c, editor_state, EditorState::CHAR);
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
  Move(c, editor_state, editor_state->structure());
}

const string LineDown::Description() {
  return "moves down one line";
}

/* static */ void LineDown::Move(
    int c, EditorState* editor_state, EditorState::Structure structure) {
  if (editor_state->direction() == BACKWARDS) {
    editor_state->set_direction(FORWARDS);
    LineUp::Move(c, editor_state, structure);
    return;
  }
  if (!editor_state->has_current_buffer()) { return; }
  switch (structure) {
    case EditorState::CHAR:
      {
        shared_ptr<OpenBuffer> buffer = editor_state->current_buffer()->second;
        size_t pos = buffer->current_position_line();
        buffer->set_current_position_line(min(pos + editor_state->repetitions(),
                                              buffer->contents()->size() - 1));
        if (editor_state->repetitions() > 1) {
          // Saving on single-lines changes makes this very verbose, lets avoid that.
          editor_state->PushCurrentPosition();
        }
      }
      break;

    case EditorState::WORD:
      // Move in whole pages.
      editor_state->set_repetitions(
          editor_state->repetitions() * editor_state->visible_lines());
      Move(c, editor_state, EditorState::CHAR);
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
  Move(c, editor_state, editor_state->structure());
}

const string PageUp::Description() {
  return "moves up one page";
}

void PageUp::ProcessInput(int c, EditorState* editor_state) {
  editor_state->set_repetitions(
      editor_state->repetitions() * editor_state->visible_lines());
  editor_state->ResetStructure();
  LineUp::Move(c, editor_state, editor_state->structure());
}

const string PageDown::Description() {
  return "moves down one page";
}

void PageDown::ProcessInput(int c, EditorState* editor_state) {
  editor_state->set_repetitions(
      editor_state->repetitions() * editor_state->visible_lines());
  editor_state->ResetStructure();
  LineDown::Move(c, editor_state, editor_state->structure());
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
        buffer->set_current_position_col(min(
            buffer->current_position_col() + editor_state->repetitions(),
            buffer->current_line()->size()));
        if (editor_state->repetitions() > 1) {
          editor_state->PushCurrentPosition();
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
        editor_state->PushCurrentPosition();
        editor_state->ResetRepetitions();
        editor_state->ResetStructure();
        editor_state->ResetDirection();
      }
      break;

    case EditorState::SEARCH:
      SearchHandler(
          editor_state->current_buffer()->second->position(),
          editor_state->last_search_query(), editor_state);
      editor_state->ResetStructure();
      break;

    default:
      LineDown::Move(c, editor_state,
          EditorState::LowerStructure(
              EditorState::LowerStructure(editor_state->structure())));
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
        if (buffer->current_position_col() > buffer->current_line()->size()) {
          buffer->set_current_position_col(buffer->current_line()->size());
        }
        if (buffer->current_position_col() > editor_state->repetitions()) {
          buffer->set_current_position_col(
              buffer->current_position_col() - editor_state->repetitions());
        } else {
          buffer->set_current_position_col(0);
        }

        if (editor_state->repetitions() > 1) {
          editor_state->PushCurrentPosition();
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

        editor_state->PushCurrentPosition();
        editor_state->ResetRepetitions();
        editor_state->ResetStructure();
        editor_state->ResetDirection();
      }
      break;

    case EditorState::SEARCH:
      editor_state->set_direction(BACKWARDS);
      SearchHandler(
          editor_state->current_buffer()->second->position(),
          editor_state->last_search_query(), editor_state);
      editor_state->ResetStructure();
      break;

    default:
      LineUp::Move(c, editor_state,
          EditorState::LowerStructure(
              EditorState::LowerStructure(editor_state->structure())));
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
    if (editor_state->direction() == FORWARDS) {
      editor_state->set_direction(BACKWARDS);
    } else if (editor_state->default_direction() == FORWARDS) {
      editor_state->set_default_direction(BACKWARDS);
    } else {
      editor_state->set_default_direction(FORWARDS);
      editor_state->ResetDirection();
    }
  }
};

void SetRepetitions(EditorState* editor_state, int number) {
  editor_state->set_repetitions(number);
}

class SetStructureCommand : public Command {
 public:
  SetStructureCommand(EditorState::Structure value, const string& description)
      : value_(value), description_(description) {}

  const string Description() {
    return "sets the structure: " + description_;
  }

  void ProcessInput(int c, EditorState* editor_state) {
    if (editor_state->structure() != value_) {
      editor_state->set_structure(value_);
      editor_state->set_sticky_structure(false);
    } else if (!editor_state->sticky_structure()) {
      editor_state->set_sticky_structure(true);
    } else {
      editor_state->set_structure(EditorState::CHAR);
      editor_state->set_sticky_structure(false);
    }
  }

 private:
  EditorState::Structure value_;
  const string description_;
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

      OpenFileOptions options;
      options.editor_state = editor_state;
      options.path = line;
      options.ignore_if_not_found = true;
      OpenFile(options);
    }
  }
};

class StartSearchMode : public Command {
 public:
  const string Description() {
    return "Searches for a string.";
  }

  void ProcessInput(int c, EditorState* editor_state) {
    switch (editor_state->structure()) {
      case EditorState::WORD:
        {
          editor_state->ResetStructure();
          if (!editor_state->has_current_buffer()) { return; }
          auto buffer = editor_state->current_buffer()->second;
          LineColumn start, end;
          if (!buffer->BoundWordAt(buffer->position(), &start, &end)) {
            return;
          }
          assert(start.line == end.line);
          assert(start.column + 1 < end.column);
          if (start.line != buffer->position().line
              || start.column > buffer->position().column) {
            buffer->set_position(start);
          }
          SearchHandler(
              buffer->position(),
              Substring(buffer->LineAt(start.line)->contents,
                        start.column, end.column - start.column)
                  ->ToString(),
              editor_state);
        }
        break;

      default:
        auto position = editor_state->current_buffer()->second->position();
        Prompt(editor_state, "/", "search", "",
               [position](const string& input, EditorState* editor_state) {
                 SearchHandler(position, input, editor_state);
               },
               SearchHandlerPredictor);
        break;
    }
  }
};

class ResetStateCommand : public Command {
 public:
  const string Description() {
    return "Resets the state of the editor.";
  }

  void ProcessInput(int c, EditorState* editor_state) {
    editor_state->ResetMode();
    editor_state->set_structure(EditorState::CHAR);
    editor_state->ResetRepetitions();
    editor_state->set_default_direction(FORWARDS);
    editor_state->ResetDirection();
  }
};

void RunCppFileHandler(const string& input, EditorState* editor_state) {
  editor_state->ResetMode();
  if (!editor_state->has_current_buffer()) { return; }
  auto buffer = editor_state->current_buffer()->second;
  for (size_t i = 0; i < editor_state->repetitions(); i++) {
    buffer->EvaluateFile(editor_state, input);
  }
  editor_state->ResetRepetitions();
}

class RunCppFileCommand : public Command {
  const string Description() {
    return "runs a command from a file";
  }

  void ProcessInput(int c, EditorState* editor_state) {
    if (!editor_state->has_current_buffer()) { return; }
    auto buffer = editor_state->current_buffer()->second;
    Prompt(editor_state, "cmd < ", "editor_commands",
           buffer->read_string_variable(
               OpenBuffer::variable_editor_commands_path()),
           RunCppFileHandler, FilePredictor);
  }
};

class SwitchCaseTransformation : public Transformation {
 public:
  SwitchCaseTransformation(const LineColumn& position)
      : position_(position) {}

  unique_ptr<Transformation> Apply(
      EditorState* editor_state, OpenBuffer* buffer) const {
    auto line = buffer->LineAt(position_.line)->contents;
    int c = line->get(position_.column);
    buffer->contents()->at(position_.line).reset(new Line(
        StringAppend(
            Substring(line, 0, position_.column),
            StringAppend(
                NewCopyString(string(1, isupper(c) ? tolower(c) : toupper(c))),
                Substring(line, position_.column + 1,
                          line->size() - (position_.column + 1))))));
    editor_state->ScheduleRedraw();
    return unique_ptr<Transformation>(new SwitchCaseTransformation(position_));
  }

 private:
  LineColumn position_;
};

class SwitchCaseCommand : public Command {
 public:
  const string Description() {
    return "Switches the case of the current character.";
  }

  void ProcessInput(int c, EditorState* editor_state) {
    if (!editor_state->has_current_buffer()) { return; }
    auto buffer = editor_state->current_buffer()->second;
    auto line = buffer->current_line();
    auto position = buffer->position();

    // Advance.
    if (position.column == line->contents->size()) {
      if (position.line + 1 == buffer->contents()->size()) {
        return;
      }
      buffer->set_position(LineColumn(position.line + 1));
    } else {
      buffer->set_position(LineColumn(position.line, position.column + 1));
    }

    if (position.column >= line->contents->size()
        || !isalpha(line->contents->get(position.column))) {
      return;
    }

    SwitchCaseTransformation transformation(position);
    editor_state->ApplyToCurrentBuffer(transformation);
  }
};

static const map<int, Command*>& GetCommandModeMap() {
  static map<int, Command*> output;
  if (output.empty()) {
    output.insert(make_pair('a', new EnterAdvancedMode()));
    output.insert(make_pair('i', new EnterInsertMode()));
    output.insert(make_pair('f', new EnterFindMode()));
    output.insert(make_pair('r', new ReverseDirectionCommand()));

    output.insert(make_pair('/', new StartSearchMode()));
    output.insert(make_pair('g', new GotoCommand()));

    output.insert(make_pair('w', new SetStructureCommand(EditorState::WORD, "word")));
    output.insert(make_pair('e', new SetStructureCommand(EditorState::LINE, "line")));
    output.insert(make_pair('E', new SetStructureCommand(EditorState::PAGE, "page")));
    output.insert(make_pair('F', new SetStructureCommand(EditorState::SEARCH, "search")));
    output.insert(make_pair('B', new SetStructureCommand(EditorState::BUFFER, "buffer")));

    output.insert(make_pair('d', new Delete()));
    output.insert(make_pair('p', new Paste()));
    output.insert(make_pair('u', new UndoCommand()));
    output.insert(make_pair('\n', new ActivateLink()));

    output.insert(make_pair('c', new RunCppFileCommand()));

    output.insert(make_pair('b', new GotoPreviousPositionCommand()));
    output.insert(make_pair('j', new LineDown()));
    output.insert(make_pair('k', new LineUp()));
    output.insert(make_pair('l', new MoveForwards()));
    output.insert(make_pair('h', new MoveBackwards()));

    output.insert(make_pair('~', new SwitchCaseCommand()));

    output.insert(make_pair('?', NewHelpCommand(output, "command mode").release()));

    output.insert(make_pair(Terminal::ESCAPE, new ResetStateCommand()));

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
