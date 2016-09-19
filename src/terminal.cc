#include "terminal.h"

#include <algorithm>
#include <cassert>
#include <cctype>
#include <iostream>

extern "C" {
#include <ncursesw/curses.h>
}

#include "line_marks.h"

namespace afc {
namespace editor {

using std::cerr;
using std::set;
using std::to_wstring;

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
  init_pair(4, COLOR_BLUE, COLOR_BLACK);
  init_pair(7, COLOR_CYAN, COLOR_BLACK);
  SetStatus(L"Initializing...");
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
  if (buffer->read_bool_variable(OpenBuffer::variable_atomic_lines())
      && buffer->last_highlighted_line() != buffer->position().line) {
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
  wstring status;
  auto modifiers = editor_state.modifiers();
  if (editor_state.has_current_buffer()) {
    auto buffer = editor_state.current_buffer()->second;
    status.push_back('[');
    if (buffer->current_position_line() >= buffer->contents()->size()) {
      status += L"<EOF>";
    } else {
      status += to_wstring(buffer->current_position_line() + 1);
    }
    status += L" of " + to_wstring(buffer->contents()->size()) + L", "
        + to_wstring(buffer->current_position_col() + 1);

    if (modifiers.has_region_start) {
      status += L" R:";
      const auto& buffer_name = modifiers.region_start.buffer_name;
      if (buffer_name != editor_state.current_buffer()->first) {
        status += buffer_name + L":";
      }
      const auto& position = modifiers.region_start.position;
      status += to_wstring(position.line + 1) + L":"
          + to_wstring(position.column + 1);
    }

    status += L"] ";

    auto active_cursors = buffer->active_cursors()->size();
    if (active_cursors != 1) {
      status += L" cursors:" + to_wstring(active_cursors) + L" ";
    }

    wstring flags = buffer->FlagsString();
    Modifiers modifiers(editor_state.modifiers());
    if (editor_state.repetitions() != 1) {
      flags += to_wstring(editor_state.repetitions());
    }
    if (modifiers.default_direction == BACKWARDS) {
      flags += L" REVERSE";
    } else if (modifiers.direction == BACKWARDS) {
      flags += L" reverse";
    }

    if (modifiers.default_insertion == Modifiers::REPLACE) {
      flags += L" REPLACE";
    } else if (modifiers.insertion == Modifiers::REPLACE) {
      flags += L" replace";
    }

    switch (modifiers.strength) {
      case Modifiers::WEAK:
        flags += L" w";
        break;
      case Modifiers::VERY_WEAK:
        flags += L" W";
        break;
      case Modifiers::STRONG:
        flags += L" s";
        break;
      case Modifiers::VERY_STRONG:
        flags += L" S";
        break;
      case Modifiers::DEFAULT:
        break;
    }

    wstring structure;
    switch (editor_state.structure()) {
      case CHAR:
        break;
      case WORD:
        structure = L"word";
        break;
      case LINE:
        structure = L"line";
        break;
      case MARK:
        structure = L"mark";
        break;
      case PAGE:
        structure = L"page";
        break;
      case SEARCH:
        structure = L"search";
        break;
      case BUFFER:
        structure = L"buffer";
        break;
      case CURSOR:
        structure = L"cursor";
        break;
    }
    if (!structure.empty()) {
      if (editor_state.sticky_structure()) {
        transform(structure.begin(), structure.end(), structure.begin(),
                  ::toupper);
      }
      switch (editor_state.structure_range()) {
        case Modifiers::ENTIRE_STRUCTURE:
          break;
        case Modifiers::FROM_BEGINNING_TO_CURRENT_POSITION:
          structure = L"[..." + structure;
          break;
        case Modifiers::FROM_CURRENT_POSITION_TO_END:
          structure = structure + L"...]";
          break;
      }
      flags += L"(" + structure + L")";
    }

    if (!flags.empty()) {
      status += L" " + flags + L" ";
    }

    if (editor_state.status().empty()) {
      status += L"“" + GetBufferContext(editor_state, buffer) + L"” ";
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
      status += L"run:" + to_wstring(running) + L" ";
    }
    if (failed > 0) {
      status += L"fail:" + to_wstring(failed) + L" ";
    }
  }

  int y, x;
  getyx(stdscr, y, x);
  status += editor_state.status();
  x += status.size();
  if (status.size() < static_cast<size_t>(COLS)) {
    status += wstring(COLS - status.size(), ' ');
  } else if (status.size() > static_cast<size_t>(COLS)) {
    status = status.substr(0, COLS);
  }
  addwstr(status.c_str());
  if (editor_state.status_prompt()) {
    move(y, x);
  }
}

wstring Terminal::GetBufferContext(
    const EditorState& editor_state,
    const shared_ptr<OpenBuffer>& buffer) {
  auto marks = buffer->GetLineMarks(editor_state);
  auto current_line_marks = marks->find(buffer->position().line);
  if (current_line_marks != marks->end()) {
    auto mark = current_line_marks->second;
    auto source = editor_state.buffers()->find(mark.source);
    if (source != editor_state.buffers()->end()
        && source->second->contents()->size() > mark.source_line) {
      return source->second->contents()->at(mark.source_line)->ToString();
    }
  }
  return buffer->name();
}

class LineOutputReceiver : public Line::OutputReceiverInterface {
 public:
  void AddCharacter(wchar_t c) {
    cchar_t cchar;
    wchar_t input[] = { c, L'0' };
    setcchar(&cchar, input, 0, 0, nullptr);
    add_wch(&cchar);
  }
  void AddString(const wstring& str) {
    addwstr(str.c_str());
  }
  void AddModifier(Line::Modifier modifier) {
    switch (modifier) {
      case Line::RESET:
        attroff(A_BOLD);
        attroff(A_DIM);
        attroff(A_UNDERLINE);
        attroff(A_REVERSE);
        attroff(COLOR_PAIR(1));
        attroff(COLOR_PAIR(2));
        attroff(COLOR_PAIR(3));
        attroff(COLOR_PAIR(4));
        attroff(COLOR_PAIR(7));
        break;
      case Line::BOLD:
        attron(A_BOLD);
        break;
      case Line::DIM:
        attron(A_DIM);
        break;
      case Line::UNDERLINE:
        attron(A_UNDERLINE);
        break;
      case Line::REVERSE:
        attron(A_REVERSE);
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
      case Line::BLUE:
        attron(COLOR_PAIR(4));
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

class HighlightedLineOutputReceiver : public Line::OutputReceiverInterface {
 public:
  HighlightedLineOutputReceiver(Line::OutputReceiverInterface* delegate)
      : delegate_(delegate) {
    delegate_->AddModifier(Line::REVERSE);
  }

  void AddCharacter(wchar_t c) { delegate_->AddCharacter(c); }
  void AddString(const wstring& str) { delegate_->AddString(str); }
  void AddModifier(Line::Modifier modifier) {
    switch (modifier) {
      case Line::RESET:
        delegate_->AddModifier(Line::RESET);
        delegate_->AddModifier(Line::REVERSE);
        break;
      default:
        delegate_->AddModifier(modifier);
    }
  }
  size_t width() const {
    return delegate_->width();
  }
 private:
  Line::OutputReceiverInterface* const delegate_;
};

class CursorsHighlighter : public Line::OutputReceiverInterface {
 public:
  CursorsHighlighter(Line::OutputReceiverInterface* delegate,
                     size_t current_column,
                     set<size_t>::const_iterator first,
                     set<size_t>::const_iterator last,
                     bool multiple_cursors)
      : delegate_(delegate),
        current_column_(current_column),
        first_(first),
        last_(last),
        multiple_cursors_(multiple_cursors) {
    CheckInvariants();
  }

  void AddCharacter(wchar_t c) {
    CheckInvariants();
    bool at_cursor = first_ != last_ && *first_ == position_;
    if (at_cursor) {
      ++first_;
      CHECK(first_ == last_ || *first_ > position_);
      AddModifier(Line::REVERSE);
      if (current_column_ != position_) {
        AddModifier(multiple_cursors_ ? Line::CYAN : Line::BLUE);
      }
    }

    delegate_->AddCharacter(c);
    position_++;

    if (at_cursor) {
      AddModifier(Line::RESET);
    }
    CheckInvariants();
  }

  void AddString(const wstring& str) {
    size_t str_pos = 0;
    while (str_pos < str.size()) {
      CheckInvariants();
      DCHECK_GE(position_, str_pos);

      // Compute the position of the next cursor relative to the start of this
      // string.
      size_t next_cursor = (first_ == last_)
          ? str.size() : *first_ + str_pos - position_;
      if (next_cursor > str_pos) {
        size_t len = next_cursor - str_pos;
        delegate_->AddString(str.substr(str_pos, len));
        str_pos += len;
        position_ += len;
      }

      CheckInvariants();

      if (str_pos < str.size()) {
        CHECK(first_ != last_);
        CHECK_EQ(*first_, position_);
        AddCharacter(str[str_pos]);
        str_pos++;
      }
      CheckInvariants();
    }
  }

  void AddModifier(Line::Modifier modifier) {
    delegate_->AddModifier(modifier);
  }

  size_t width() const {
    return delegate_->width();
  }
 private:
  void CheckInvariants() {
    if (first_ != last_) {
      CHECK_GE(*first_, position_);
    }
  }

  Line::OutputReceiverInterface* const delegate_;
  // The position (column) of the main cursor, assuming that the main cursor is
  // in the current line. This is used to avoid any highlighting at that
  // position, since the terminal takes care of that automatically (we don't
  // have to do anything). If the main cursor is not in the current line, the
  // customers of this class should just pass the maximum value, so that this
  // class will show all cursors.
  const size_t current_column_;

  // Given a set of the columns with cursors in the current line, this is an
  // iterator to the first element in a column greater than or equal to
  // position_.
  set<size_t>::const_iterator first_;

  // Iterator past the end of the set of columns in the current line that have
  // cursors.
  set<size_t>::const_iterator last_;
  const bool multiple_cursors_;
  size_t position_ = 0;
};

void Terminal::ShowBuffer(const EditorState* editor_state) {
  const shared_ptr<OpenBuffer> buffer = editor_state->current_buffer()->second;
  const Tree<shared_ptr<Line>>& contents(*buffer->contents());

  move(0, 0);

  LineOutputReceiver line_output_receiver;

  size_t lines_to_show = static_cast<size_t>(LINES);
  size_t current_line = buffer->view_start_line();
  size_t lines_shown = 0;
  buffer->set_last_highlighted_line(-1);

  // Key is line number.
  std::map<size_t, std::set<size_t>> cursors;
  for (auto cursor : *buffer->active_cursors()) {
    size_t absolute_line = cursor.first - buffer->contents()->begin();
    if (LineColumn(absolute_line, cursor.second) != buffer->position()) {
      cursors[absolute_line].insert(cursor.second);
    }
  }

  while (lines_shown < lines_to_show) {
    Line::OutputReceiverInterface* receiver = &line_output_receiver;
    if (current_line >= contents.size()) {
      addwstr(L"\n");
      lines_shown++;
      continue;
    }
    if (!buffer->IsLineFiltered(current_line)) {
      current_line ++;
      continue;
    }

    std::unique_ptr<Line::OutputReceiverInterface> atomic_lines_highlighter;

    auto current_cursors = cursors.find(current_line);
    std::unique_ptr<Line::OutputReceiverInterface> cursors_highlighter;

    lines_shown++;
    const shared_ptr<Line> line(contents[current_line]);
    CHECK(line->contents() != nullptr);
    if (current_line == buffer->position().line
        && buffer->read_bool_variable(OpenBuffer::variable_atomic_lines())) {
      buffer->set_last_highlighted_line(current_line);
      atomic_lines_highlighter.reset(
          new HighlightedLineOutputReceiver(receiver));
      receiver = atomic_lines_highlighter.get();
    } else if (current_cursors != cursors.end()) {
      size_t line =
          buffer->current_cursor()->first - buffer->contents()->begin();
      size_t current_column = line == current_line
          ? buffer->current_cursor()->second
          : std::numeric_limits<size_t>::max();
      LOG(INFO) << "Cursors in current line: "
                << current_cursors->second.size();
      cursors_highlighter.reset(new CursorsHighlighter(
          receiver, current_column, current_cursors->second.begin(),
          current_cursors->second.end(),
          buffer->read_bool_variable(buffer->variable_multiple_cursors())));
      receiver = cursors_highlighter.get();
    }
    line->Output(editor_state, buffer, current_line, receiver);
    // Need to do this for atomic lines, since they override the Reset modifier
    // with Reset + Reverse.
    line_output_receiver.AddModifier(Line::RESET);
    current_line ++;
  }
}

void Terminal::AdjustPosition(const shared_ptr<OpenBuffer> buffer) {
  if (buffer->read_bool_variable(OpenBuffer::variable_atomic_lines())) {
    curs_set(0);
    return;
  } else {
    curs_set(1);
  }
  const Tree<shared_ptr<Line>>& contents(*buffer->contents());
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

wint_t Terminal::Read(EditorState*) {
  while (true) {
    int c = getch();
    DVLOG(5) << "Read: " << c << "\n";
    if (c == -1) {
      return c;
    } else if (c == KEY_RESIZE) {
      return KEY_RESIZE;
    }
    wchar_t output;
    char input[1] = { static_cast<char>(c) };
    switch (mbrtowc(&output, input, 1, &mbstate_)) {
      case 1:
        VLOG(4) << "Finished reading wide character: " << output;
        break;
      case -1:
        LOG(WARNING) << "Encoding error occurred, ignoring input: " << c;
        return -1;
      case -2:
        VLOG(5) << "Incomplete (but valid) mbs, reading further.";
        continue;
      default:
        LOG(FATAL) << "Unexpected return value from mbrtowc.";
    }
    switch (output) {
      case 127:
        return BACKSPACE;

      case 4:
        return CHAR_EOF;

      case 0x0c:
        return CTRL_L;

      case 21:
        return CTRL_U;

      case 22:
        return CTRL_V;

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
        return output;
    }
  }
}

void Terminal::SetStatus(const std::wstring& status) {
  status_ = status;

  size_t height = LINES;
  size_t width = COLS;
  move(height - 1, 0);
  std::wstring output_status =
      status_.length() > width
      ? status_.substr(0, width)
      : (status_ + std::wstring(width - status_.length(), ' '));
  addwstr(output_status.c_str());
  move(0, 0);
  refresh();
}

}  // namespace afc
}  // namespace editor
