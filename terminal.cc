#include "terminal.h"

#include <algorithm>
#include <cassert>
#include <cctype>
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
constexpr int Terminal::CHAR_EOF;

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
  size_t line =
      min(buffer->current_position_line(), buffer->contents()->size());
  if (buffer->view_start_line() > line) {
    buffer->set_view_start_line(line);
    editor_state->ScheduleRedraw();
  } else if (buffer->view_start_line() + LINES - 1 <= line) {
    buffer->set_view_start_line(line - LINES + 2);
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

    string structure;
    switch (editor_state.structure()) {
      case EditorState::CHAR:
        break;
      case EditorState::WORD:
        structure = "word";
        break;
      case EditorState::LINE:
        structure = "line";
        break;
      case EditorState::PAGE:
        structure = "page";
        break;
      case EditorState::SEARCH:
        structure = "search";
        break;
      case EditorState::BUFFER:
        structure = "buffer";
        break;
    }
    if (!structure.empty()) {
      if (editor_state.sticky_structure()) {
        transform(structure.begin(), structure.end(), structure.begin(), ::toupper);
      }
      flags += "(" + structure + ")";
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

class LineOutputReceiver : public Line::OutputReceiverInterface {
 public:
  void AddCharacter(int c) {
    addch(c);
  }
  void AddString(const string& str) {
    addstr(str.c_str());
  }
  size_t width() const {
    return COLS;
  }
};

void Terminal::ShowBuffer(const EditorState* editor_state) {
  const shared_ptr<OpenBuffer> buffer = editor_state->current_buffer()->second;
  const vector<shared_ptr<Line>>& contents(*buffer->contents());

  move(0, 0);

  LineOutputReceiver receiver;

  size_t lines_to_show = static_cast<size_t>(LINES);
  size_t current_line = buffer->view_start_line();
  size_t lines_shown = 0;
  while (lines_shown < lines_to_show) {
    if (current_line == contents.size()) {
      addch('\n');
      lines_shown++;
      continue;
    }
    if (!buffer->IsLineFiltered(current_line)) {
      current_line ++;
      continue;
    }

    lines_shown++;
    const shared_ptr<Line> line(contents[current_line]);
    assert(line->contents() != nullptr);
    line->Output(editor_state, buffer, &receiver);
    current_line ++;
  }
}

void Terminal::AdjustPosition(const shared_ptr<OpenBuffer> buffer) {
  const vector<shared_ptr<Line>>& contents(*buffer->contents());
  size_t position_line = min(buffer->position().line, contents.size());
  size_t line_length =
      position_line == contents.size()
      || !buffer->IsLineFiltered(buffer->position().line)
      ? 0 : contents[position_line]->size();
  size_t pos_x = min(min(static_cast<size_t>(COLS) - 1, line_length),
                     buffer->position().column);

  size_t pos_y = 0;
  for (size_t line = buffer->view_start_line(); line < position_line; line++) {
    if (buffer->IsLineFiltered(line)) {
      pos_y++;
    }
  }
  move(pos_y, pos_x);
}

int Terminal::Read(EditorState* editor_state) {
  int c = getch();
  //cerr << "Read: " << c << "\n";
  switch (c) {
    case 127:
      return BACKSPACE;

    case 4:
      return CHAR_EOF;

    case 0x0c:
      cerr << "Redraw\n";
      wrefresh(curscr);
      editor_state->ScheduleRedraw();
      return -1;

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
