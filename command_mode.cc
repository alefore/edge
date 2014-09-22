#include <cmath>
#include <functional>
#include <fstream>
#include <iostream>
#include <memory>
#include <map>
#include <string>

#include <glog/logging.h>

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
#include "secondary_mode.h"
#include "substring.h"
#include "terminal.h"
#include "transformation.h"
#include "transformation_move.h"

namespace {
using std::advance;
using std::ceil;
using std::make_pair;
using namespace afc::editor;

class GotoCommand : public Command {
 public:
  GotoCommand(size_t calls) : calls_(calls % 4) {}

  const string Description() {
    return "goes to Rth structure from the beginning";
  }

  void ProcessInput(int c, EditorState* editor_state) {
    if (c != 'g') {
      editor_state->ResetMode();
      editor_state->ProcessInput(c);
      return;
    }
    if (!editor_state->has_current_buffer()) { return; }
    shared_ptr<OpenBuffer> buffer = editor_state->current_buffer()->second;
    switch (editor_state->structure()) {
      case EditorState::CHAR:
        {
          if (buffer->current_line() == nullptr) { return; }
          const string line_prefix_characters = buffer->read_string_variable(
              OpenBuffer::variable_line_prefix_characters());
          const auto& line = buffer->current_line();
          size_t start = 0;
          while (start < line->size()
                 && (line_prefix_characters.find(line->get(start))
                     != string::npos)) {
            start++;
          }
          size_t end = line->size();
          while (start + 1 < end
                 && (line_prefix_characters.find(line->get(end - 1))
                     != string::npos)) {
            end--;
          }
          size_t position = ComputePosition(
              start, end, line->size(), editor_state->direction(),
              editor_state->repetitions(), calls_);
          assert(position <= line->size());
          buffer->set_current_position_col(position);
        }
        break;

      case EditorState::WORD:
        {
          // TODO: Handle reverse direction
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
            } else if (end.column == buffer->LineAt(position.line)->size()) {
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
          size_t lines = buffer->contents()->size();
          size_t position = ComputePosition(
              0, lines, lines, editor_state->direction(),
              editor_state->repetitions(), calls_);
          assert(position <= buffer->contents()->size());
          buffer->set_current_position_line(position);
        }
        break;

      case EditorState::PAGE:
        {
          CHECK(!buffer->contents()->empty());
          size_t pages = ceil(static_cast<double>(buffer->contents()->size())
              / editor_state->visible_lines());
          size_t position = editor_state->visible_lines() * ComputePosition(
              0, pages, pages, editor_state->direction(),
              editor_state->repetitions(), calls_);
          CHECK_LT(position, buffer->contents()->size());
          buffer->set_current_position_line(position);
        }
        break;

      case EditorState::SEARCH:
        // TODO: Implement.
        break;

      case EditorState::BUFFER:
        {
          size_t buffers = editor_state->buffers()->size();
          size_t position = ComputePosition(
              0, buffers, buffers, editor_state->direction(),
              editor_state->repetitions(), calls_);
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
    editor_state->set_mode(unique_ptr<Command>(new GotoCommand(calls_ + 1)));
  }

 private:
  size_t ComputePosition(
      size_t prefix_len, size_t suffix_start, size_t elements,
      Direction direction, size_t repetitions, size_t calls) {
    CHECK_LE(prefix_len, suffix_start);
    CHECK_LE(suffix_start, elements);
    if (calls > 1) {
      return ComputePosition(
          prefix_len, suffix_start, elements, ReverseDirection(direction),
          repetitions, calls - 2);
    }
    if (calls == 1) {
      return ComputePosition(0, elements, elements, direction, repetitions, 0);
    }
    if (direction == FORWARDS) {
      return min(prefix_len + repetitions - 1, elements);
    } else {
      return suffix_start - min(suffix_start, repetitions - 1);
    }
  }

  const size_t calls_;
};

class Delete : public Command {
 public:
  const string Description() {
    return "deletes the current item (char, word, line ...)";
  }

  void ProcessInput(int, EditorState* editor_state) {
    if (!editor_state->has_current_buffer()) { return; }
    shared_ptr<OpenBuffer> buffer = editor_state->current_buffer()->second;

    switch (editor_state->structure()) {
      case EditorState::CHAR:
        if (editor_state->has_current_buffer()) {
          auto buffer = editor_state->current_buffer()->second;
          editor_state->ApplyToCurrentBuffer(
              NewDeleteCharactersTransformation(
                  editor_state->repetitions(), true));
          editor_state->ScheduleRedraw();
        }
        break;

      case EditorState::WORD:
        if (editor_state->has_current_buffer()) {
          auto buffer = editor_state->current_buffer()->second;
          editor_state->ApplyToCurrentBuffer(
              NewDeleteWordsTransformation(editor_state->repetitions(), true));
          editor_state->ScheduleRedraw();
        }
        break;

      case EditorState::LINE:
        if (editor_state->has_current_buffer()) {
          auto buffer = editor_state->current_buffer()->second;
          editor_state->ApplyToCurrentBuffer(
              NewDeleteLinesTransformation(editor_state->repetitions(), true));
          editor_state->ScheduleRedraw();
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
};

// TODO: Replace with insert.  Insert should be called 'type'.
class Paste : public Command {
 public:
  const string Description() {
    return "pastes the last deleted text";
  }

  void ProcessInput(int, EditorState* editor_state) {
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
    buffer->CheckPosition();
    buffer->MaybeAdjustPositionCol();
    editor_state->ApplyToCurrentBuffer(NewInsertBufferTransformation(
        it->second, editor_state->repetitions(), END));
    editor_state->ResetRepetitions();
    editor_state->ScheduleRedraw();
  }
};

class UndoCommand : public Command {
 public:
  const string Description() {
    return "undoes the last change to the current buffer";
  }

  void ProcessInput(int, EditorState* editor_state) {
    if (!editor_state->has_current_buffer()) { return; }
    editor_state->current_buffer()->second->Undo(editor_state);
    editor_state->ResetRepetitions();
    editor_state->ResetDirection();
    editor_state->ScheduleRedraw();
  }
};

class GotoPreviousPositionCommand : public Command {
 public:
  const string Description() {
    return "go back to previous position";
  }

  void ProcessInput(int, EditorState* editor_state) {
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
  switch (structure) {
    case EditorState::CHAR:
      {
        shared_ptr<OpenBuffer> buffer = editor_state->current_buffer()->second;
        buffer->CheckPosition();
        const auto line_begin = buffer->line_begin();
        while (editor_state->repetitions() && buffer->line() != line_begin) {
          buffer->LineUp();
          editor_state->set_repetitions(editor_state->repetitions() - 1);
        }
        editor_state->PushCurrentPosition();
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
        buffer->CheckPosition();
        const auto line_end = buffer->line_end();
        while (editor_state->repetitions() && buffer->line() != line_end) {
          buffer->LineDown();
          editor_state->set_repetitions(editor_state->repetitions() - 1);
        }
        editor_state->PushCurrentPosition();
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
  switch (editor_state->structure()) {
    case EditorState::CHAR:
      {
        if (!editor_state->has_current_buffer()) { return; }
        editor_state->ApplyToCurrentBuffer(NewMoveTransformation());
        editor_state->ResetRepetitions();
        editor_state->ResetStructure();
        editor_state->ResetDirection();
      }
      break;

    case EditorState::WORD:
      {
        if (!editor_state->has_current_buffer()) { return; }
        shared_ptr<OpenBuffer> buffer = editor_state->current_buffer()->second;
        buffer->CheckPosition();
        buffer->MaybeAdjustPositionCol();
        if (buffer->current_line() == nullptr) { return; }
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
                 && (buffer->current_position_col() ==
                         buffer->current_line()->size()
                     || word_characters.find(buffer->current_character()) ==
                            string::npos)) {
            if (buffer->current_position_col() ==
                    buffer->current_line()->size()) {
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
        editor_state->set_direction(
            ReverseDirection(editor_state->direction()));
        MoveForwards::Move(c, editor_state);
        return;
      }
      break;

    case EditorState::WORD:
      {
        if (!editor_state->has_current_buffer()) { return; }
        shared_ptr<OpenBuffer> buffer = editor_state->current_buffer()->second;
        buffer->CheckPosition();
        if (buffer->current_line() == nullptr) { return; }
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
              buffer->set_current_position_col(buffer->current_line()->size());
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

  void ProcessInput(int, EditorState* editor_state) {
    afc::editor::EnterInsertMode(editor_state);
  }
};

class EnterAdvancedMode : public Command {
 public:
  const string Description() {
    return "enters advanced-command mode (press 'a?' for more)";
  }

  void ProcessInput(int, EditorState* editor_state) {
    editor_state->set_mode(NewAdvancedMode());
  }
};

class EnterSecondaryMode : public Command {
 public:
  const string Description() {
    return "enters secondary-command mode (press 's?' for more)";
  }

  void ProcessInput(int, EditorState* editor_state) {
    editor_state->set_mode(NewSecondaryMode());
  }
};

class EnterFindMode : public Command {
 public:
  const string Description() {
    return "finds occurrences of a character";
  }

  void ProcessInput(int, EditorState* editor_state) {
    editor_state->set_mode(NewFindMode());
  }
};

class ReverseDirectionCommand : public Command {
 public:
  const string Description() {
    return "reverses the direction of the next command";
  }

  void ProcessInput(int, EditorState* editor_state) {
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

  void ProcessInput(int, EditorState* editor_state) {
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
    if (buffer->current_line()->activate() != nullptr) {
      buffer->current_line()->activate()->ProcessInput(c, editor_state);
    } else {
      buffer->MaybeAdjustPositionCol();
      string line = buffer->current_line()->ToString();

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

  void ProcessInput(int, EditorState* editor_state) {
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
              buffer->LineAt(start.line)
                  ->Substring(start.column, end.column - start.column)
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

  void ProcessInput(int, EditorState* editor_state) {
    editor_state->ResetMode();
    editor_state->set_structure(EditorState::CHAR);
    editor_state->ResetRepetitions();
    editor_state->set_default_direction(FORWARDS);
    editor_state->ResetDirection();
  }
};

class HardRedrawCommand : public Command {
 public:
  const string Description() {
    return "Redraws the screen";
  }

  void ProcessInput(int, EditorState* editor_state) {
    editor_state->set_screen_needs_hard_redraw(true);
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

  void ProcessInput(int, EditorState* editor_state) {
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
  unique_ptr<Transformation> Apply(
      EditorState* editor_state, OpenBuffer* buffer) const {
    unique_ptr<TransformationStack> stack(new TransformationStack);
    if (buffer->position().line < buffer->contents()->size()
        && buffer->position().column < (*buffer->line())->size()) {
      int c = (*buffer->line())->get(buffer->position().column);
      shared_ptr<OpenBuffer> buffer_to_insert(
          new OpenBuffer(editor_state, "- text inserted"));
      buffer_to_insert->AppendLine(editor_state,
          NewCopyString(string(1, isupper(c) ? tolower(c) : toupper(c))));
      editor_state->ScheduleRedraw();

      stack->PushBack(NewDeleteCharactersTransformation(1, false));
      stack->PushBack(NewInsertBufferTransformation(buffer_to_insert, 1, END));
    }

    LineColumn position = buffer->position();
    switch (editor_state->direction()) {
      case FORWARDS:
        if (position.line >= buffer->contents()->size()) {
          // Pass.
        } else if (position.column < (*buffer->line())->size()) {
          position.column++;
        } else {
          position = LineColumn(position.line + 1);
        }
        break;
      case BACKWARDS:
        if (position == LineColumn(0)) {
          // Pass.
        } else if (position.line >= buffer->contents()->size()
                   || position.column == 0) {
          size_t line = min(position.line, buffer->contents()->size()) - 1;
          position = LineColumn(line, buffer->LineAt(line)->size());
        } else {
          position.column --;
        }
    }
    stack->PushBack(NewGotoPositionTransformation(position));
    return stack->Apply(editor_state, buffer);
  }

  unique_ptr<Transformation> Clone() {
    return unique_ptr<Transformation>(new SwitchCaseTransformation());
  }

  virtual bool ModifiesBuffer() { return true; }
};

class SwitchCaseCommand : public Command {
 public:
  const string Description() {
    return "Switches the case of the current character.";
  }

  void ProcessInput(int, EditorState* editor_state) {
    if (!editor_state->has_current_buffer()) { return; }
    auto buffer = editor_state->current_buffer()->second;
    auto line = buffer->current_line();

    editor_state->ApplyToCurrentBuffer(
        unique_ptr<Transformation>(new SwitchCaseTransformation()));
  }
};

class RepeatLastTransformationCommand : public Command {
 public:
  const string Description() {
    return "Repeats the last command.";
  }

  void ProcessInput(int, EditorState* editor_state) {
    if (!editor_state->has_current_buffer()) { return; }
    editor_state
        ->current_buffer()->second->RepeatLastTransformation(editor_state);
    editor_state->ScheduleRedraw();
  }
};

static const map<int, Command*>& GetCommandModeMap() {
  static map<int, Command*> output;
  if (output.empty()) {
    output.insert(make_pair('a', new EnterAdvancedMode()));
    output.insert(make_pair('s', new EnterSecondaryMode()));
    output.insert(make_pair('i', new EnterInsertMode()));
    output.insert(make_pair('f', new EnterFindMode()));
    output.insert(make_pair('r', new ReverseDirectionCommand()));

    output.insert(make_pair('/', new StartSearchMode()));
    output.insert(make_pair('g', new GotoCommand(0)));

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

    output.insert(make_pair('.', new RepeatLastTransformationCommand()));
    output.insert(make_pair('?', NewHelpCommand(output, "command mode").release()));

    output.insert(make_pair(Terminal::ESCAPE, new ResetStateCommand()));

    output.insert(make_pair(Terminal::CTRL_L, new HardRedrawCommand()));
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
