#include "src/screen_curses.h"

extern "C" {
#include <ncursesw/curses.h>
}

#include "src/line_column.h"
#include "src/terminal.h"

namespace afc {
namespace editor {
namespace {
class ScreenCurses : public Screen {
 public:
  ScreenCurses() {
    initscr();
    noecho();
    nodelay(stdscr, true);
    keypad(stdscr, false);
    start_color();
    init_pair(1, COLOR_BLACK, COLOR_BLACK);
    init_pair(2, COLOR_RED, COLOR_BLACK);
    init_pair(3, COLOR_GREEN, COLOR_BLACK);
    init_pair(4, COLOR_BLUE, COLOR_BLACK);
    init_pair(5, COLOR_YELLOW, COLOR_BLACK);
    init_pair(6, COLOR_MAGENTA, COLOR_BLACK);
    init_pair(7, COLOR_CYAN, COLOR_BLACK);
    init_pair(8, COLOR_WHITE, COLOR_RED);
    init_pair(9, COLOR_WHITE, COLOR_BLACK);
  }

  ~ScreenCurses() { endwin(); }

  void Flush() override {}

  void HardRefresh() override { wrefresh(curscr); }

  void Refresh() override { refresh(); }

  void Clear() override { clear(); }

  void SetCursorVisibility(CursorVisibility cursor_visibility) override {
    switch (cursor_visibility) {
      case INVISIBLE:
        curs_set(0);
        break;
      case NORMAL:
        curs_set(1);
        break;
    }
  }

  void Move(LineNumber y, ColumnNumber x) override { move(y.line, x.column); }
  void WriteString(const wstring& s) override { addwstr(s.c_str()); }

  void SetModifier(LineModifier modifier) override {
    switch (modifier) {
      case LineModifier::RESET:
        attroff(A_BOLD);
        attroff(A_ITALIC);
        attroff(A_DIM);
        attroff(A_UNDERLINE);
        attroff(A_REVERSE);
        attroff(COLOR_PAIR(1));
        attroff(COLOR_PAIR(2));
        attroff(COLOR_PAIR(3));
        attroff(COLOR_PAIR(4));
        attroff(COLOR_PAIR(5));
        attroff(COLOR_PAIR(6));
        attroff(COLOR_PAIR(7));
        attroff(COLOR_PAIR(8));
        attroff(COLOR_PAIR(9));
        break;
      case LineModifier::BOLD:
        attron(A_BOLD);
        break;
      case LineModifier::ITALIC:
        attron(A_ITALIC);
        break;
      case LineModifier::DIM:
        attron(A_DIM);
        break;
      case LineModifier::UNDERLINE:
        attron(A_UNDERLINE);
        break;
      case LineModifier::REVERSE:
        attron(A_REVERSE);
        break;
      case LineModifier::BLACK:
        attron(COLOR_PAIR(1));
        break;
      case LineModifier::RED:
        attron(COLOR_PAIR(2));
        break;
      case LineModifier::GREEN:
        attron(COLOR_PAIR(3));
        break;
      case LineModifier::BLUE:
        attron(COLOR_PAIR(4));
        break;
      case LineModifier::YELLOW:
        attron(COLOR_PAIR(5));
        break;
      case LineModifier::MAGENTA:
        attron(COLOR_PAIR(6));
        break;
      case LineModifier::CYAN:
        attron(COLOR_PAIR(7));
        break;
      case LineModifier::BG_RED:
        attron(COLOR_PAIR(8));
        break;
      case LineModifier::WHITE:
        attron(COLOR_PAIR(9));
        break;
    }
  }

  LineNumberDelta lines() const override { return LineNumberDelta(LINES); }
  ColumnNumberDelta columns() const override { return ColumnNumberDelta(COLS); }
};
}  // namespace

wint_t ReadChar(std::mbstate_t* mbstate) {
  while (true) {
    int c = getch();
    DVLOG(5) << "Read: " << c << "\n";
    if (c == -1) {
      return c;
    } else if (c == KEY_RESIZE) {
      return KEY_RESIZE;
    }
    wchar_t output;
    char input[1] = {static_cast<char>(c)};
    CHECK(mbstate != nullptr);
    switch (static_cast<int>(mbrtowc(&output, input, 1, mbstate))) {
      case 1:
        VLOG(4) << "Finished reading wide character: "
                << std::wstring(1, output);
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
        return Terminal::BACKSPACE;

      case 1:
        return Terminal::CTRL_A;

      case 4:
        return Terminal::CTRL_D;

      case 5:
        return Terminal::CTRL_E;

      case 0x0b:
        return Terminal::CTRL_K;

      case 0x0c:
        return Terminal::CTRL_L;

      case 21:
        return Terminal::CTRL_U;

      case 22:
        return Terminal::CTRL_V;

      case 27: {
        int next = getch();
        // cerr << "Read next: " << next << "\n";
        switch (next) {
          case -1:
            return Terminal::ESCAPE;

          case '[': {
            int next2 = getch();
            // cerr << "Read next2: " << next2 << "\n";
            switch (next2) {
              case 51:
                getch();
                return Terminal::DELETE;
              case 53:
                getch();
                return Terminal::PAGE_UP;
              case 54:
                getch();
                return Terminal::PAGE_DOWN;
              case 'A':
                return Terminal::UP_ARROW;
              case 'B':
                return Terminal::DOWN_ARROW;
              case 'C':
                return Terminal::RIGHT_ARROW;
              case 'D':
                return Terminal::LEFT_ARROW;
            }
          }
            return -1;
        }
        // cerr << "Unget: " << next << "\n";
        ungetch(next);
      }
        return Terminal::ESCAPE;
      default:
        return output;
    }
  }
}

std::unique_ptr<Screen> NewScreenCurses() {
  return std::make_unique<ScreenCurses>();
}

}  // namespace editor
}  // namespace afc
