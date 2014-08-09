extern "C" {
#include <curses.h>
#include <term.h>
}

#include "terminal.h"

namespace afc {
namespace editor {

Terminal::Terminal() {
  initscr();
  noecho();
  SetStatus("Initializing...");
}

Terminal::~Terminal() {
  endwin();
}

void Terminal::Display(EditorState* editor_state) {
  clear();
  const shared_ptr<OpenBuffer> open_buffer(editor_state->buffers[0]);
  const vector<shared_ptr<Line>>& contents(open_buffer->contents);

  if (open_buffer->view_start_line > open_buffer->current_position_line) {
    open_buffer->view_start_line = open_buffer->current_position_line;
  } else if (open_buffer->view_start_line + LINES <= open_buffer->current_position_line) {
    open_buffer->view_start_line = open_buffer->current_position_line - LINES + 1;
  }

  size_t last_line_to_show =
      open_buffer->view_start_line + static_cast<size_t>(LINES);
  if (last_line_to_show > contents.size()) {
    last_line_to_show = contents.size() - 1;
  }
  for (size_t current_line = open_buffer->view_start_line;
       current_line < last_line_to_show; current_line++) {
    const shared_ptr<LazyString> line(contents[current_line]->contents);
    int size = std::min(static_cast<size_t>(COLS), line->size());
    for (size_t pos = 0; pos < size; pos++) {
      addch(line->get(pos));
    }
    addch('\n');
  }

  size_t pos_x = open_buffer->current_position_col;
  if (pos_x > contents[open_buffer->current_position_line]->contents->size()) {
    pos_x = contents[open_buffer->current_position_line]->contents->size();
  }

  move(open_buffer->current_position_line - open_buffer->view_start_line, pos_x);

  refresh();
}

int Terminal::Read() {
  return getch();
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
