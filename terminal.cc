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

const int Terminal::DOWN_ARROW = -2;
const int Terminal::UP_ARROW = -3;
const int Terminal::LEFT_ARROW = -4;
const int Terminal::RIGHT_ARROW = -5;

Terminal::Terminal() {
  initscr();
  noecho();
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
      ShowStatus(editor_state->status);
      refresh();
    }
    return;
  }
  auto const& buffer = editor_state->get_current_buffer();
  if (buffer->view_start_line > buffer->current_position_line) {
    buffer->view_start_line = buffer->current_position_line;
    editor_state->screen_needs_redraw = true;
  } else if (buffer->view_start_line + LINES <= buffer->current_position_line) {
    buffer->view_start_line = buffer->current_position_line - LINES + 1;
    editor_state->screen_needs_redraw = true;
  }

  if (editor_state->screen_needs_redraw) {
    ShowBuffer(buffer);
    editor_state->screen_needs_redraw = false;
  }
  ShowStatus(editor_state->status);
  AdjustPosition(buffer);
  refresh();
}

void Terminal::ShowStatus(const string& status) {
  move(LINES - 1, 0);
  if (status.size() < COLS) {
    addstr(status.c_str());
    for (int i = status.size(); i < COLS; i++) {
      addch(' ');
    }
  } else {
    addstr(status.substr(0, COLS).c_str());
  }
}

void Terminal::ShowBuffer(const shared_ptr<OpenBuffer> buffer) {
  const vector<shared_ptr<Line>>& contents(buffer->contents);

  clear();

  size_t last_line_to_show =
      buffer->view_start_line + static_cast<size_t>(LINES) - 1;
  if (last_line_to_show >= contents.size()) {
    last_line_to_show = contents.size() - 1;
  }
  for (size_t current_line = buffer->view_start_line;
       current_line <= last_line_to_show; current_line++) {
    const shared_ptr<LazyString> line(contents[current_line]->contents);
    int size = std::min(static_cast<size_t>(COLS), line->size());
    for (size_t pos = 0; pos < size; pos++) {
      int c = line->get(pos);
      assert(c != '\n');
      addch(c);
    }
    addch('\n');
  }
}

void Terminal::AdjustPosition(const shared_ptr<OpenBuffer> buffer) {
  const vector<shared_ptr<Line>>& contents(buffer->contents);
  size_t pos_x = buffer->current_position_col;
  if (pos_x > contents[buffer->current_position_line]->contents->size()) {
    pos_x = contents[buffer->current_position_line]->contents->size();
  }

  move(buffer->current_position_line - buffer->view_start_line, pos_x);
}

int Terminal::Read() {
  int c = getch();
  //cerr << "Read: " << c << "\n";
  if (c != 27) {
    return c;
  }
  nodelay(stdscr, true);
  int next = getch();
  //cerr << "Read next: " << next << "\n";
  nodelay(stdscr, false);
  switch (next) {
    case -1:
      return -1;

    case 91:
      {
        int next2 = getch();
        //cerr << "Read next2: " << next2 << "\n";
        switch (next2) {
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

    default:
      //cerr << "Unget: " << next << "\n";
      ungetch(next);
      return -1;
  }
}

void Terminal::SetStatus(const std::string& status) {
  status_ = status;

  auto height = LINES;
  auto width = COLS;
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
