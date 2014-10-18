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
constexpr int Terminal::CTRL_L;
constexpr int Terminal::CTRL_U;
constexpr int Terminal::CHAR_EOF;

Terminal::Terminal() {
  initscr();
  noecho();
  nodelay(stdscr, true);
  keypad(stdscr, false);
  start_color();
  init_pair(1, COLOR_BLACK, COLOR_BLACK);
  init_pair(2, COLOR_RED, COLOR_BLACK);
  init_pair(3, COLOR_GREEN, COLOR_BLACK);
  init_pair(7, COLOR_CYAN, COLOR_BLACK);
  SetStatus("Initializing...");
}

Terminal::~Terminal() {
  endwin();
}

void Terminal::Display(EditorState* editor_state) {
  if (editor_state->screen_needs_hard_redraw()) {
    wrefresh(curscr);
    editor_state->set_screen_needs_hard_redraw(false),
    editor_state->ScheduleRedraw();
  }
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
      min(buffer->current_position_line(), buffer->contents()->size() - 1);
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
  string status;
  if (editor_state.has_current_buffer()) {
    auto buffer = editor_state.current_buffer()->second;
    status.push_back('[');
    if (buffer->current_position_line() >= buffer->contents()->size()) {
      status += "<EOF>";
    } else {
      status += to_string(buffer->current_position_line() + 1);
    }
    status += " of " + to_string(buffer->contents()->size()) + ", "
        + to_string(buffer->current_position_col() + 1) + "] ";

    string flags = buffer->FlagsString();
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
      case CHAR:
        break;
      case WORD:
        structure = "word";
        break;
      case LINE:
        structure = "line";
        break;
      case PAGE:
        structure = "page";
        break;
      case SEARCH:
        structure = "search";
        break;
      case BUFFER:
        structure = "buffer";
        break;
    }
    if (!structure.empty()) {
      if (editor_state.sticky_structure()) {
        transform(structure.begin(), structure.end(), structure.begin(),
                  ::toupper);
      }
      switch (editor_state.structure_modifier()) {
        case ENTIRE_STRUCTURE:
          break;
        case FROM_BEGINNING_TO_CURRENT_POSITION:
          structure = "[..." + structure;
          break;
        case FROM_CURRENT_POSITION_TO_END:
          structure = structure + "...]";
          break;
      }
      flags += "(" + structure + ")";
    }

    if (!flags.empty()) {
      flags += " ";
      status += " " + flags;
    }
  }

  {
    int running = 0;
    int failed = 0;
    for (const auto& it : *editor_state.buffers()) {
      if (it.second->child_pid() != -1) {
        running++;
      } else {
        int status = it.second->child_exit_status();
        if (WIFEXITED(status)) {
          if (WEXITSTATUS(status)) {
            failed++;
          }
        }
      }
    }
    if (running > 0) {
      status += "run:" + to_string(running) + " ";
    }
    if (failed > 0) {
      status += "fail:" + to_string(failed) + " ";
    }
  }

  int y, x;
  getyx(stdscr, y, x);
  status += editor_state.status();
  x += status.size();
  if (status.size() < static_cast<size_t>(COLS)) {
    status += string(COLS - status.size(), ' ');
  } else if (status.size() > static_cast<size_t>(COLS)) {
    status = status.substr(0, COLS);
  }
  addstr(status.c_str());
  if (editor_state.status_prompt()) {
    move(y, x);
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
  void AddModifier(Line::Modifier modifier) {
    switch (modifier) {
      case Line::RESET:
        attroff(A_BOLD);
        attroff(COLOR_PAIR(1));
        attroff(COLOR_PAIR(2));
        attroff(COLOR_PAIR(3));
        attroff(COLOR_PAIR(7));
        break;
      case Line::BOLD:
        attron(A_BOLD);
        break;
      case Line::BLACK:
        attron(COLOR_PAIR(1));
        break;
      case Line::RED:
        attron(COLOR_PAIR(2));
        break;
      case Line::GREEN:
        attron(COLOR_PAIR(3));
        break;
      case Line::CYAN:
        attron(COLOR_PAIR(7));
        break;
    }
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
    receiver.AddModifier(Line::RESET);
    current_line ++;
  }
}

void Terminal::AdjustPosition(const shared_ptr<OpenBuffer> buffer) {
  const vector<shared_ptr<Line>>& contents(*buffer->contents());
  size_t position_line = min(buffer->position().line, contents.size() - 1);
  size_t line_length;
  if (contents.empty()) {
    line_length = 0;
  } else if (buffer->position().line >= contents.size()) {
    line_length = (*contents.rbegin())->size();
  } else if (!buffer->IsLineFiltered(buffer->position().line)) {
    line_length = 0;
  } else {
    line_length = contents[position_line]->size();
  }
  size_t pos_x = min(static_cast<size_t>(COLS) - 1, line_length);
  if (buffer->position().line < contents.size()) {
    pos_x = min(pos_x, buffer->position().column);
  }

  size_t pos_y = 0;
  for (size_t line = buffer->view_start_line(); line < position_line; line++) {
    if (buffer->IsLineFiltered(line)) {
      pos_y++;
    }
  }
  move(pos_y, pos_x);
}

int Terminal::Read(EditorState*) {
  int c = getch();
  //cerr << "Read: " << c << "\n";
  switch (c) {
    case 127:
      return BACKSPACE;

    case 4:
      return CHAR_EOF;

    case 0x0c:
      return CTRL_L;

    case 21:
      return CTRL_U;

    case 27:
      {
        int next = getch();
        //cerr << "Read next: " << next << "\n";
        switch (next) {
          case -1:
            return ESCAPE;

          case '[':
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
                case 'A':
                  return UP_ARROW;
                case 'B':
                  return DOWN_ARROW;
                case 'C':
                  return RIGHT_ARROW;
                case 'D':
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
