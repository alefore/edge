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
          if (buffer->contents()->empty()) { return; }
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
          if (buffer->contents()->empty()) { return; }
          size_t position =
              ComputePosition(editor_state, buffer->contents()->size());
          assert(position < buffer->contents()->size());
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
        DeleteCharacters(c, editor_state);
        break;

      case EditorState::WORD:
        // TODO: Implement.
        editor_state->SetStatus("Oops, delete word is not yet implemented.");
        break;

      case EditorState::LINE:
        DeleteLines(c, editor_state);
        break;

      case EditorState::PAGE:
        // TODO: Implement.
        editor_state->SetStatus("Oops, delete page is not yet implemented.");
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
    editor_state->ScheduleRedraw();
    editor_state->ResetRepetitions();
  }

 private:
  void DeleteLines(int c, EditorState* editor_state) {
    shared_ptr<OpenBuffer> buffer = editor_state->current_buffer()->second;
    if (buffer->contents()->empty()) { return; }
    shared_ptr<OpenBuffer> deleted_text(new OpenBuffer(kPasteBuffer));

    assert(buffer->current_position_line() < buffer->contents()->size());

    auto first_line = buffer->contents()->begin() + buffer->current_position_line();
    vector<shared_ptr<Line>>::iterator last_line;
    if (buffer->current_position_line() + editor_state->repetitions()
        > buffer->contents()->size()) {
      last_line = buffer->contents()->end();
    } else {
      last_line = first_line + editor_state->repetitions();
    }
    for (auto it = first_line; it < last_line; ++it) {
      if ((*it)->activate.get() != nullptr) {
        (*it)->activate->ProcessInput('d', editor_state);
      }
    }
    deleted_text->contents()->insert(
        deleted_text->contents()->end(), first_line, last_line);
    buffer->contents()->erase(first_line, last_line);
    buffer->set_modified(true);
    buffer->CheckPosition();
    InsertDeletedTextBuffer(editor_state, deleted_text);
  }

  void DeleteCharacters(int c, EditorState* editor_state) {
    shared_ptr<OpenBuffer> buffer = editor_state->current_buffer()->second;
    if (buffer->contents()->empty()) { return; }
    shared_ptr<OpenBuffer> deleted_text(new OpenBuffer(kPasteBuffer));

    if (buffer->current_position_col() > buffer->current_line()->size()) {
      buffer->set_current_position_col(buffer->current_line()->size());
    }

    while (editor_state->repetitions() > 0) {
      auto current_line = buffer->contents()->begin() + buffer->current_position_line();
      shared_ptr<LazyString> suffix = EmptyString();
      size_t characters_left =
          (*current_line)->contents->size() - buffer->current_position_col();

      if (buffer->at_last_line()
          && editor_state->repetitions() > characters_left) {
        editor_state->set_repetitions(characters_left);
        if (editor_state->repetitions() == 0) { continue; }
      }
      buffer->set_modified(true);

      if (editor_state->repetitions() <= characters_left) {
        deleted_text->AppendLine(Substring(
            (*current_line)->contents,
            buffer->current_position_col(),
            editor_state->repetitions()));
        (*current_line)->contents = StringAppend(
            Substring(
                (*current_line)->contents,
                0,
                buffer->current_position_col()),
            Substring(
                (*current_line)->contents,
                buffer->current_position_col() + editor_state->repetitions()));
        editor_state->set_repetitions(0);
        continue;
      }

      if (buffer->at_beginning_of_line()) {
        deleted_text->AppendLine((*current_line)->contents);
        assert(editor_state->repetitions() >= (*current_line)->size() + 1);
        editor_state->set_repetitions(
            editor_state->repetitions() - (*current_line)->size() - 1);
        buffer->contents()->erase(current_line);
        continue;
      }

      auto next_line =
          buffer->contents()->begin() + buffer->current_position_line() + 1;

      if (buffer->atomic_lines() && (*next_line)->contents->size() > 0) {
        editor_state->set_repetitions(0);
        continue;
      }

      deleted_text->AppendLine(
          Substring((*current_line)->contents, buffer->current_position_col()));
      buffer->replace_current_line(shared_ptr<Line>(new Line(
          StringAppend(buffer->current_line_head(), (*next_line)->contents))));
      assert(editor_state->repetitions() >= characters_left + 1);
      editor_state->set_repetitions(
          editor_state->repetitions() - characters_left - 1);
      buffer->contents()->erase(next_line);
    }
    InsertDeletedTextBuffer(editor_state, deleted_text);
  }

  void InsertDeletedTextBuffer(
      EditorState* editor_state, const shared_ptr<OpenBuffer>& buffer) {
    auto insert_result = editor_state->buffers()->insert(make_pair(
        kPasteBuffer, buffer));
    if (!insert_result.second) {
      insert_result.first->second = buffer;
    }
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

class GotoPreviousPositionCommand : public Command {
 public:
  const string Description() {
    return "go back to previous position";
  }

  void ProcessInput(int c, EditorState* editor_state) {
    while (editor_state->repetitions() > 0
           && editor_state->HasPositionsInStack()) {
      const Position pos = editor_state->PopBackPosition();
      auto it = editor_state->buffers()->find(pos.buffer);
      if (it != editor_state->buffers()->end()
          && (pos.buffer != editor_state->current_buffer()->first
              || (editor_state->structure() <= 1
                  && pos.line != editor_state->current_buffer()->second->current_position_line())
              || editor_state->structure() <= 0)) {
        editor_state->set_current_buffer(it);
        it->second->set_current_position_line(pos.line);
        it->second->set_current_position_col(pos.col);
        it->second->Enter(editor_state);
        editor_state->ScheduleRedraw();
        editor_state->set_repetitions(editor_state->repetitions() - 1);
      }
    }
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
        if (buffer->contents()->empty()) { return; }
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
        if (buffer->contents()->empty()) { return; }
        if (editor_state->repetitions() > 1) {
          // Saving on single-lines changes makes this very verbose, lets avoid that.
          editor_state->PushCurrentPosition();
        }
        size_t pos = buffer->current_position_line();
        if (pos + editor_state->repetitions() < buffer->contents()->size() - 1) {
          buffer->set_current_position_line(pos + editor_state->repetitions());
        } else {
          buffer->set_current_position_line(buffer->contents()->size() - 1);
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
      editor_state->MoveBufferForwards(editor_state->repetitions());
      editor_state->ScheduleRedraw();
  }
  editor_state->ResetStructure();
  editor_state->ResetRepetitions();
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
        if (buffer->contents()->empty()) { return; }
        if (editor_state->repetitions() > 1) {
          editor_state->PushCurrentPosition();
        }
        if (buffer->current_position_col() + editor_state->repetitions()
            <= buffer->current_line()->size()) {
          buffer->set_current_position_col(
              buffer->current_position_col() + editor_state->repetitions());
        } else {
          buffer->set_current_position_col(buffer->current_line()->size());
        }

        editor_state->ResetRepetitions();
        editor_state->ResetStructure();
      }
      break;

    case EditorState::WORD:
      {
        if (!editor_state->has_current_buffer()) { return; }
        shared_ptr<OpenBuffer> buffer = editor_state->current_buffer()->second;
        if (buffer->contents()->empty()) { return; }
        buffer->CheckPosition();
        buffer->MaybeAdjustPositionCol();
        editor_state->PushCurrentPosition();
        const bool* is_word = buffer->word_characters();
        while (editor_state->repetitions() > 0) {
          // Seek forwards until we're in a whitespace.
          while (buffer->current_position_col() < buffer->current_line()->size()
                 && is_word[static_cast<int>(buffer->current_character())]) {
            buffer->set_current_position_col(buffer->current_position_col() + 1);
          }

          // Seek forwards until we're in a non-whitespace.
          bool advanced = false;
          while (!buffer->at_end()
                 && (buffer->current_position_col() == buffer->current_line()->contents->size()
                     || !is_word[static_cast<int>(buffer->current_character())])) {
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
      }
      break;

    default:
      editor_state->set_structure(
          EditorState::LowerStructure(
              EditorState::LowerStructure(editor_state->structure())));
      LineDown::Move(c, editor_state);
  }
};

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
        if (buffer->contents()->empty()) { return; }
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
      }
      break;

    case EditorState::WORD:
      {
        if (!editor_state->has_current_buffer()) { return; }
        shared_ptr<OpenBuffer> buffer = editor_state->current_buffer()->second;
        if (buffer->contents()->empty()) { return; }
        buffer->CheckPosition();
        buffer->MaybeAdjustPositionCol();
        editor_state->PushCurrentPosition();
        const bool* is_word = buffer->word_characters();
        while (editor_state->repetitions() > 0) {
          // Seek backwards until we're just after a whitespace.
          while (buffer->current_position_col() > 0
                 && is_word[static_cast<int>(buffer->previous_character())]) {
            buffer->set_current_position_col(buffer->current_position_col() - 1);
          }

          // Seek backwards until we're just after a non-whitespace.
          bool advanced = false;
          while (!buffer->at_beginning()
                 && (buffer->current_position_col() == 0
                     || !is_word[static_cast<int>(buffer->previous_character())])) {
            if (buffer->current_position_col() == 0) {
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
        if (buffer->current_position_col() != 0) {
          buffer->set_current_position_col(buffer->current_position_col() - 1);
        }

        editor_state->ResetRepetitions();
        editor_state->ResetStructure();
      }
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

class ReverseDirection : public Command {
 public:
  const string Description() {
    return "reverses the direction of the next command";
  }

  void ProcessInput(int c, EditorState* editor_state) {
    editor_state->set_direction(
        editor_state->direction() == FORWARDS ? BACKWARDS : FORWARDS);
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
        editor_state->set_structure(EditorState::CHAR);
        break;
      case 'w':
        editor_state->set_structure(EditorState::WORD);
        break;
      case 'l':
        editor_state->set_structure(EditorState::LINE);
        break;
      case 'p':
        editor_state->set_structure(EditorState::PAGE);
        break;
      case 'b':
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
      case 'P':
        editor_state->set_default_structure(EditorState::PAGE);
        break;
      case 'B':
        editor_state->set_default_structure(EditorState::BUFFER);
        break;
      case Terminal::ESCAPE:
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
    if (buffer->contents()->empty()) { return; }
    if (buffer->current_line()->activate.get() != nullptr) {
      buffer->current_line()->activate->ProcessInput(c, editor_state);
    } else {
      buffer->MaybeAdjustPositionCol();
      string line = buffer->current_line()->contents->ToString();

      size_t start = line.find_last_of(' ', buffer->current_position_col());
      if (start != line.npos) {
        line = line.substr(start + 1);
      }

      size_t end = line.find_first_of(' ');
      if (end != line.npos) {
        line = line.substr(0, end);
      }

      unique_ptr<EditorMode> mode = NewFileLinkMode(line, 0, true);
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
    output.insert(make_pair('r', new ReverseDirection()));

    output.insert(make_pair(
        '/',
        NewLinePromptCommand("/", "searches for a string", SearchHandler).release()));

    output.insert(make_pair('g', new GotoCommand()));
    output.insert(make_pair('d', new Delete()));
    output.insert(make_pair('p', new Paste()));
    output.insert(make_pair('\n', new ActivateLink()));

    output.insert(make_pair('b', new GotoPreviousPositionCommand()));
    output.insert(make_pair('j', new LineDown()));
    output.insert(make_pair('k', new LineUp()));
    output.insert(make_pair('l', new MoveForwards()));
    output.insert(make_pair('h', new MoveBackwards()));

    output.insert(make_pair('s', new EnterStructureMode()));
    output.insert(make_pair(
        'r',
        new NumberMode("repeats the next command", SetRepetitions)));

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
