#include "terminal.h"

#include <cassert>
#include <iostream>

extern "C" {
#include <curses.h>
#include <term.h>
}

namespace afc {
namespace editor {

using std::cerr;
using std::to_string;

constexpr int Terminal::DOWN_ARROW;
constexpr int Terminal::UP_ARROW;
constexpr int Terminal::LEFT_ARROW;
constexpr int Terminal::RIGHT_ARROW;
constexpr int Terminal::BACKSPACE;
constexpr int Terminal::PAGE_UP;
constexpr int Terminal::PAGE_DOWN;

Terminal::Terminal() {
  initscr();
  noecho();
  nodelay(stdscr, true);
  keypad(stdscr, false);
  SetStatus("Initializing...");
}

Terminal::~Terminal() {
  endwin();
}

void Terminal::Display(EditorState* editor_state) {
  if (editor_state->current_buffer == editor_state->buffers.end()) {
    if (editor_state->screen_needs_redraw) {
      editor_state->screen_needs_redraw = false;
      clear();
    }
    ShowStatus(*editor_state);
    refresh();
    return;
  }
  auto const& buffer = editor_state->get_current_buffer();
  if (buffer->view_start_line() > buffer->current_position_line()) {
    buffer->set_view_start_line(buffer->current_position_line());
    editor_state->screen_needs_redraw = true;
  } else if (buffer->view_start_line() + LINES - 1 <= buffer->current_position_line()) {
    buffer->set_view_start_line(buffer->current_position_line() - LINES + 2);
    editor_state->screen_needs_redraw = true;
  }

  if (editor_state->screen_needs_redraw) {
    ShowBuffer(editor_state);
    editor_state->screen_needs_redraw = false;
  }
  ShowStatus(*editor_state);
  if (!editor_state->status_prompt) {
    AdjustPosition(buffer);
  }
  refresh();
  editor_state->visible_lines = static_cast<size_t>(LINES);
}

void Terminal::ShowStatus(const EditorState& editor_state) {
  move(LINES - 1, 0);
  if (editor_state.current_buffer != editor_state.buffers.end()) {
    auto buffer = editor_state.current_buffer->second;
    addch('[');
    addstr(to_string(buffer->current_position_line() + 1).c_str());
    addstr(" of ");
    addstr(to_string(buffer->contents()->size()).c_str());
    addstr(", ");
    addstr(to_string(buffer->current_position_col() + 1).c_str());
    addstr("] ");

    string flags(buffer->FlagsString());
    if (editor_state.repetitions != 1) {
      flags += to_string(editor_state.repetitions);
    }
    if (editor_state.direction == BACKWARDS) {
      flags += "r";
    }

    switch (editor_state.default_structure) {
      case 0:
        switch (editor_state.structure) {
          case 0:
            break;
          case 1:
            flags += "l";
            break;
          case 2:
            flags += "b";
            break;
        }
        break;
      case 1:
        flags += "L";
        break;
      case 2:
        flags += "B";
        break;
    }

    if (!flags.empty()) {
      flags += " ";
      addstr(flags.c_str());
    }
  }
  int y, x;
  getyx(stdscr, y, x);
  if (x >= COLS) { return; }
  size_t chars_left = COLS - x;
  if (editor_state.status.size() < chars_left) {
    addstr(editor_state.status.c_str());
    for (size_t i = editor_state.status.size(); i < chars_left; i++) {
      addch(' ');
    }
    if (editor_state.status_prompt) {
      move(y, x + editor_state.status.size());
    }
  } else {
    addstr(editor_state.status.substr(0, chars_left).c_str());
  }
}

void Terminal::ShowBuffer(const EditorState* editor_state) {
  const shared_ptr<OpenBuffer> buffer = editor_state->get_current_buffer();
  const vector<shared_ptr<Line>>& contents(*buffer->contents());

  clear();

  size_t view_stop_line =
      buffer->view_start_line() + static_cast<size_t>(LINES)
      - (editor_state->status.empty() ? 0 : 1);
  if (view_stop_line > contents.size()) {
    view_stop_line = contents.size();
  }
  for (size_t current_line = buffer->view_start_line();
       current_line < view_stop_line; current_line++) {
    assert(current_line < contents.size());
    const shared_ptr<LazyString> line(contents[current_line]->contents);
    assert(line.get() != nullptr);
    size_t size = std::min(static_cast<size_t>(COLS), line->size());
    for (size_t pos = 0; pos < size; pos++) {
      int c = line->get(pos);
      assert(c != '\n');
      addch(c);
    }
    if (size < static_cast<size_t>(COLS)) {
      addch('\n');
    }
  }
}

void Terminal::AdjustPosition(const shared_ptr<OpenBuffer> buffer) {
  const vector<shared_ptr<Line>>& contents(*buffer->contents());
  size_t pos_x = buffer->current_position_col();
  assert(buffer->current_position_line() <
         contents.empty() ? 1 : contents.size());
  size_t line_length = contents.empty()
      ? 0 : contents[buffer->current_position_line()]->contents->size();
  if (pos_x > line_length) {
    pos_x = line_length;
  }

  move(buffer->current_position_line() - buffer->view_start_line(), pos_x);
}

int Terminal::Read() {
  int c = getch();
  //cerr << "Read: " << c << "\n";
  switch (c) {
    case 127:
      return BACKSPACE;
    case 27:
      {
        int next = getch();
        //cerr << "Read next: " << next << "\n";
        switch (next) {
          case -1:
            return ESCAPE;

          case 91:
            {
              int next2 = getch();
              //cerr << "Read next2: " << next2 << "\n";
              switch (next2) {
                case 53:
                  return PAGE_UP;
                case 54:
                  return PAGE_DOWN;
                case 65:
                  return UP_ARROW;
                case 66:
                  return DOWN_ARROW;
                case 67:
                  return RIGHT_ARROW;
                case 68:
                  return LEFT_ARROW;
              }
            }
            return -1;
        }
        //cerr << "Unget: " << next << "\n";
        ungetch(next);
      }
      return ESCAPE;
    default:
      return c;
  }
}

void Terminal::SetStatus(const std::string& status) {
  status_ = status;

  size_t height = LINES;
  size_t width = COLS;
  move(height - 1, 0);
  std::string output_status =
      status_.length() > width
      ? status_.substr(0, width)
      : (status_ + std::string(width - status_.length(), ' '));
  addstr(output_status.c_str());
  move(0, 0);
  refresh();
}

}  // namespace afc
}  // namespace editor
