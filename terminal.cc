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
constexpr int Terminal::ESCAPE;

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
  if (!editor_state->has_current_buffer()) {
    if (editor_state->screen_needs_redraw()) {
      editor_state->set_screen_needs_redraw(false);
      clear();
    }
    ShowStatus(*editor_state);
    refresh();
    return;
  }
  auto const& buffer = editor_state->current_buffer()->second;
  if (buffer->view_start_line() > buffer->current_position_line()) {
    buffer->set_view_start_line(buffer->current_position_line());
    editor_state->ScheduleRedraw();
  } else if (buffer->view_start_line() + LINES - 1 <= buffer->current_position_line()) {
    buffer->set_view_start_line(buffer->current_position_line() - LINES + 2);
    editor_state->ScheduleRedraw();
  }

  size_t desired_start_column = buffer->current_position_col()
      - min(buffer->current_position_col(), static_cast<size_t>(COLS) - 1);
  if (buffer->view_start_column() != desired_start_column) {
    buffer->set_view_start_column(desired_start_column);
    editor_state->ScheduleRedraw();
  }

  if (editor_state->screen_needs_redraw()) {
    ShowBuffer(editor_state);
    editor_state->set_screen_needs_redraw(false);
  }
  ShowStatus(*editor_state);
  if (!editor_state->status_prompt()) {
    AdjustPosition(buffer);
  }
  refresh();
  editor_state->set_visible_lines(static_cast<size_t>(LINES - 1));
}

void Terminal::ShowStatus(const EditorState& editor_state) {
  move(LINES - 1, 0);
  if (editor_state.has_current_buffer()) {
    auto buffer = editor_state.current_buffer()->second;
    addch('[');
    addstr(to_string(buffer->current_position_line() + 1).c_str());
    addstr(" of ");
    addstr(to_string(buffer->contents()->size()).c_str());
    addstr(", ");
    addstr(to_string(buffer->current_position_col() + 1).c_str());
    addstr("] ");

    string flags(buffer->FlagsString());
    if (editor_state.repetitions() != 1) {
      flags += to_string(editor_state.repetitions());
    }
    if (editor_state.default_direction() == BACKWARDS) {
      flags += "R";
    } else if (editor_state.direction() == BACKWARDS) {
      flags += "r";
    }

    switch (editor_state.default_structure()) {
      case EditorState::CHAR:
        switch (editor_state.structure()) {
          case EditorState::CHAR:
            break;
          case EditorState::WORD:
            flags += "w";
            break;
          case EditorState::LINE:
            flags += "l";
            break;
          case EditorState::PAGE:
            flags += "p";
            break;
          case EditorState::SEARCH:
            flags += "s";
            break;
          case EditorState::BUFFER:
            flags += "b";
            break;
        }
        break;
      case EditorState::WORD:
        flags += "W";
        break;
      case EditorState::LINE:
        flags += "L";
        break;
      case EditorState::PAGE:
        flags += "P";
        break;
      case EditorState::SEARCH:
        flags += "S";
        break;
      case EditorState::BUFFER:
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
  if (editor_state.status().size() < chars_left) {
    addstr(editor_state.status().c_str());
    for (size_t i = editor_state.status().size(); i < chars_left; i++) {
      addch(' ');
    }
    if (editor_state.status_prompt()) {
      move(y, x + editor_state.status().size());
    }
  } else {
    addstr(editor_state.status().substr(0, chars_left).c_str());
  }
}

void Terminal::ShowBuffer(const EditorState* editor_state) {
  const shared_ptr<OpenBuffer> buffer = editor_state->current_buffer()->second;
  const vector<shared_ptr<Line>>& contents(*buffer->contents());

  move(0, 0);

  size_t view_stop_line =
      buffer->view_start_line() + static_cast<size_t>(LINES)
      - (editor_state->status().empty() ? 0 : 1);
  for (size_t current_line = buffer->view_start_line();
       current_line < view_stop_line; current_line++) {
    size_t pos_end = 0;
    if (current_line < contents.size()) {
      const shared_ptr<LazyString> line(contents[current_line]->contents);
      assert(line.get() != nullptr);
      pos_end = std::min(
          buffer->view_start_column() + static_cast<size_t>(COLS),
          line->size());
      for (size_t pos = buffer->view_start_column(); pos < pos_end; pos++) {
        int c = line->get(pos);
        assert(c != '\n');
        if (c == '\r') { addch(' '); continue; }
        addch(c);
      }
    }
    if (pos_end < buffer->view_start_column() + static_cast<size_t>(COLS)) {
      addch('\n');
    }
  }
}

void Terminal::AdjustPosition(const shared_ptr<OpenBuffer> buffer) {
  const vector<shared_ptr<Line>>& contents(*buffer->contents());
  assert(buffer->current_position_line() <= contents.size());
  size_t line_length = buffer->current_position_line() == contents.size()
      ? 0 : contents[buffer->current_position_line()]->contents->size();
  size_t pos_x = min(min(static_cast<size_t>(COLS) - 1, line_length),
                     buffer->current_position_col());

  move(buffer->current_position_line() - buffer->view_start_line(), pos_x);
}

int Terminal::Read() {
  int c = getch();
  //cerr << "Read: " << c << "\n";
  switch (c) {
    case 127:
      return BACKSPACE;
    case 21:
      {
        int next = getch();
        switch (next) {
          case -1:
            return CTRL_U;
        }
        ungetch(next);
      }
      return 21;

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
                  getch();
                  return PAGE_UP;
                case 54:
                  getch();
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
