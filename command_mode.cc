#include <functional>
#include <memory>
#include <map>
#include <string>

#include "advanced_mode.h"
#include "command.h"
#include "command_mode.h"
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
using std::make_pair;
using namespace afc::editor;

class Quit : public Command {
 public:
  const string Description() {
    return "quits";
  }

  void ProcessInput(int c, EditorState* editor_state) {
    editor_state->terminate = true;
  }
};

class Delete : public Command {
 public:
  const string Description() {
    return "deletes the current item (char, word, line ...)";
  }

  void ProcessInput(int c, EditorState* editor_state) {
    if (editor_state->buffers.empty()) { return; }
    shared_ptr<OpenBuffer> buffer = editor_state->get_current_buffer();
    shared_ptr<OpenBuffer> new_buffer(new OpenBuffer());

    if (editor_state->structure == 0) {
      DeleteCharacters(c, editor_state);
    } else if (editor_state->structure == 1) {
      DeleteLines(c, editor_state);
    } else if (editor_state->structure == 2) {
      auto buffer_to_erase = editor_state->current_buffer;
      if (editor_state->current_buffer == editor_state->buffers.begin()) {
        editor_state->current_buffer = editor_state->buffers.end();
      }
      editor_state->current_buffer--;
      editor_state->buffers.erase(buffer_to_erase);
      if (editor_state->current_buffer == buffer_to_erase) {
        editor_state->current_buffer = editor_state->buffers.end();
        assert(editor_state->current_buffer == editor_state->buffers.end());
      }
    }

    editor_state->structure = 0;
    editor_state->screen_needs_redraw = true;
    editor_state->repetitions = 1;
  }

 private:
  void DeleteLines(int c, EditorState* editor_state) {
    shared_ptr<OpenBuffer> buffer = editor_state->get_current_buffer();
    if (buffer->contents()->empty()) { return; }
    shared_ptr<OpenBuffer> deleted_text(new OpenBuffer());

    assert(buffer->current_position_line() < buffer->contents()->size());

    auto first_line = buffer->contents()->begin() + buffer->current_position_line();
    vector<shared_ptr<Line>>::iterator last_line;
    if (buffer->current_position_line() + editor_state->repetitions
        > buffer->contents()->size()) {
      last_line = buffer->contents()->end();
    } else {
      last_line = first_line + editor_state->repetitions;
    }
    deleted_text->contents()->insert(
        deleted_text->contents()->end(), first_line, last_line);
    buffer->contents()->erase(first_line, last_line);
    InsertDeletedTextBuffer(editor_state, deleted_text);
  }

  void DeleteCharacters(int c, EditorState* editor_state) {
    shared_ptr<OpenBuffer> buffer = editor_state->get_current_buffer();
    if (buffer->contents()->empty()) { return; }
    shared_ptr<OpenBuffer> deleted_text(new OpenBuffer());

    if (buffer->current_position_col() > buffer->current_line()->size()) {
      buffer->set_current_position_col(buffer->current_line()->size());
    }

    while (editor_state->repetitions > 0) {
      size_t characters_to_erase;
      auto current_line = buffer->current_line();
      shared_ptr<LazyString> suffix;
      if (editor_state->repetitions + buffer->current_position_col()
          > current_line->size()) {
        characters_to_erase = current_line->size() - buffer->current_position_col();
        if (buffer->contents()->size() > buffer->current_position_line() + 1) {
          suffix = buffer->contents()->at(buffer->current_position_line() + 1)
              ->contents;
          buffer->contents()->erase(
              buffer->contents()->begin() + buffer->current_position_line() + 1);
          editor_state->repetitions--;
        } else {
          suffix = EmptyString();
          editor_state->repetitions = characters_to_erase;
        }
      } else {
        characters_to_erase = editor_state->repetitions;
        suffix = Substring(current_line->contents,
                           buffer->current_position_col() + characters_to_erase);
      }
      editor_state->repetitions -= characters_to_erase;
      deleted_text->AppendLine(
          Substring(current_line->contents, buffer->current_position_col(),
                    characters_to_erase));
      buffer->contents()->at(buffer->current_position_line()).reset(new Line(
          StringAppend(
              Substring(current_line->contents, 0, buffer->current_position_col()),
              suffix)));
    }
    InsertDeletedTextBuffer(editor_state, deleted_text);
  }

  void InsertDeletedTextBuffer(
      EditorState* editor_state, const shared_ptr<OpenBuffer>& buffer) {
    auto insert_result = editor_state->buffers.insert(make_pair(
        "- deleted text", buffer));
    if (!insert_result.second) {
      insert_result.first->second = buffer;
    }
  }
};

class LineUp : public Command {
 public:
  const string Description() {
    return "moves up one line";
  }

  static void Move(int c, EditorState* editor_state) {
    if (editor_state->buffers.empty()) { return; }
    shared_ptr<OpenBuffer> buffer = editor_state->get_current_buffer();
    if (buffer->contents()->empty()) { return; }
    if (editor_state->structure == 0) {
      size_t pos = buffer->current_position_line();
      if (editor_state->repetitions < pos) {
        buffer->set_current_position_line(pos - editor_state->repetitions);
      } else {
        buffer->set_current_position_line(0);
      }
    } else {
      editor_state->MoveBufferBackwards(editor_state->repetitions);
      editor_state->screen_needs_redraw = true;
    }
    editor_state->structure = 0;
    editor_state->repetitions = 1;
  }

  void ProcessInput(int c, EditorState* editor_state) {
    Move(c, editor_state);
  }
};

class LineDown : public Command {
 public:
  const string Description() {
    return "moves down one line";
  }

  static void Move(int c, EditorState* editor_state) {
    if (editor_state->buffers.empty()) { return; }
    shared_ptr<OpenBuffer> buffer = editor_state->get_current_buffer();
    if (buffer->contents()->empty()) { return; }
    if (editor_state->structure == 0) {
      size_t pos = buffer->current_position_line();
      if (pos + editor_state->repetitions < buffer->contents()->size() - 1) {
        buffer->set_current_position_line(pos + editor_state->repetitions);
      } else {
        buffer->set_current_position_line(buffer->contents()->size() - 1);
      }
    } else {
      editor_state->MoveBufferForwards(editor_state->repetitions);
      editor_state->screen_needs_redraw = true;
    }
    editor_state->structure = 0;
    editor_state->repetitions = 1;
  }

  void ProcessInput(int c, EditorState* editor_state) {
    Move(c, editor_state);
  }
};

class PageUp : public Command {
 public:
  const string Description() {
    return "moves up one page";
  }

  void ProcessInput(int c, EditorState* editor_state) {
    editor_state->repetitions *= editor_state->visible_lines;
    editor_state->structure = 0;
    LineUp::Move(c, editor_state);
  }
};

class PageDown : public Command {
 public:
  const string Description() {
    return "moves down one page";
  }

  void ProcessInput(int c, EditorState* editor_state) {
    editor_state->repetitions *= editor_state->visible_lines;
    editor_state->structure = 0;
    LineDown::Move(c, editor_state);
  }
};

class MoveForwards : public Command {
 public:
  const string Description() {
    return "moves forwards";
  }

  void ProcessInput(int c, EditorState* editor_state) {
    if (editor_state->structure == 0) {
      if (editor_state->buffers.empty()) { return; }
      shared_ptr<OpenBuffer> buffer = editor_state->get_current_buffer();
      if (buffer->contents()->empty()) { return; }

      if (buffer->current_position_col() + editor_state->repetitions
          <= buffer->current_line()->size()) {
        buffer->set_current_position_col(
            buffer->current_position_col() + editor_state->repetitions);
      } else {
        buffer->set_current_position_col(buffer->current_line()->size());
      }

      editor_state->repetitions = 1;
      editor_state->structure = 0;
    } else {
      editor_state->structure--;
      LineDown::Move(c, editor_state);
    }
  }
};

class MoveBackwards : public Command {
 public:
  const string Description() {
    return "moves backwards";
  }

  void ProcessInput(int c, EditorState* editor_state) {
    if (editor_state->structure == 0) {
      if (editor_state->buffers.empty()) { return; }
      shared_ptr<OpenBuffer> buffer = editor_state->get_current_buffer();
      if (buffer->contents()->empty()) { return; }

      if (buffer->current_position_col() > buffer->current_line()->size()) {
        buffer->set_current_position_col(buffer->current_line()->size());
      }
      if (buffer->current_position_col() > editor_state->repetitions) {
        buffer->set_current_position_col(
            buffer->current_position_col() - editor_state->repetitions);
      } else {
        buffer->set_current_position_col(0);
      }

      editor_state->repetitions = 1;
      editor_state->structure = 0;
    } else {
      editor_state->structure--;
      LineUp::Move(c, editor_state);
    }
  }
};

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
    editor_state->mode = std::move(NewAdvancedMode());
  }
};

class EnterFindMode : public Command {
 public:
  const string Description() {
    return "finds occurrences of a character";
  }

  void ProcessInput(int c, EditorState* editor_state) {
    editor_state->mode = std::move(NewFindMode());
  }
};

void SetRepetitions(EditorState* editor_state, int number) {
  editor_state->repetitions = number;
}

void SetStructure(EditorState* editor_state, int number) {
  editor_state->structure = number;
}

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
    editor_state->mode = std::move(NewRepeatMode(
        [consumer_](int c, EditorState* editor_state, int number) {
      consumer_(editor_state, number);
      editor_state->mode = std::move(NewCommandMode());
      editor_state->mode->ProcessInput(c, editor_state);
    }));
    if (c < '0' || c > '9') { return; }
    editor_state->mode->ProcessInput(c, editor_state);
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
    if (editor_state->buffers.empty()) { return; }
    shared_ptr<OpenBuffer> buffer = editor_state->get_current_buffer();
    if (buffer->current_line()->activate.get() != nullptr) {
      buffer->current_line()->activate->ProcessInput(c, editor_state);
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
    output.insert(make_pair(
        '/',
        NewLinePromptCommand("/", "searches for a string", SearchHandler).release()));

    output.insert(make_pair('d', new Delete()));
    output.insert(make_pair('\n', new ActivateLink()));

    output.insert(make_pair('j', new LineDown()));
    output.insert(make_pair('k', new LineUp()));
    output.insert(make_pair('l', new MoveForwards()));
    output.insert(make_pair('h', new MoveBackwards()));

    output.insert(make_pair(
        's',
        new NumberMode("sets the structure affected by the next command",
                       SetStructure)));
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
