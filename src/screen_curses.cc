#include "src/screen_curses.h"

extern "C" {
#include <ncursesw/curses.h>
}

#include "src/infrastructure/extended_char.h"
#include "src/language/safe_types.h"
#include "src/language/text/line_column.h"
#include "src/terminal.h"

using afc::infrastructure::ControlChar;
using afc::infrastructure::ExtendedChar;
using afc::infrastructure::screen::LineModifier;
using afc::infrastructure::screen::Screen;
using afc::language::MakeNonNullUnique;
using afc::language::NonNull;
using afc::language::lazy_string::ColumnNumberDelta;
using afc::language::lazy_string::LazyString;
using afc::language::text::LineColumn;
using afc::language::text::LineColumnDelta;
using afc::language::text::LineNumberDelta;

namespace afc::editor {
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

  void Move(LineColumn position) override {
    move(position.line.read(), position.column.read());
  }
  void WriteString(const LazyString& s) override {
    TRACK_OPERATION(ScreenCurses_WriteString);
    addwstr(s.ToString().c_str());
  }

  void SetModifier(LineModifier modifier) override {
    switch (modifier) {
      case LineModifier::kReset:
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
      case LineModifier::kBold:
        attron(A_BOLD);
        break;
      case LineModifier::kItalic:
        attron(A_ITALIC);
        break;
      case LineModifier::kDim:
        attron(A_DIM);
        break;
      case LineModifier::kUnderline:
        attron(A_UNDERLINE);
        break;
      case LineModifier::kReverse:
        attron(A_REVERSE);
        break;
      case LineModifier::kBlack:
        attron(COLOR_PAIR(1));
        break;
      case LineModifier::kRed:
        attron(COLOR_PAIR(2));
        break;
      case LineModifier::kGreen:
        attron(COLOR_PAIR(3));
        break;
      case LineModifier::kBlue:
        attron(COLOR_PAIR(4));
        break;
      case LineModifier::kYellow:
        attron(COLOR_PAIR(5));
        break;
      case LineModifier::kMagenta:
        attron(COLOR_PAIR(6));
        break;
      case LineModifier::kCyan:
        attron(COLOR_PAIR(7));
        break;
      case LineModifier::kBgRed:
        attron(COLOR_PAIR(8));
        break;
      case LineModifier::kWhite:
        attron(COLOR_PAIR(9));
        break;
    }
  }

  LineColumnDelta size() const override {
    return LineColumnDelta(LineNumberDelta(LINES), ColumnNumberDelta(COLS));
  }
};
}  // namespace

std::optional<ExtendedChar> ReadChar(std::mbstate_t* mbstate) {
  while (true) {
    int c = getch();
    DVLOG(5) << "Read: " << c << "\n";
    if (c == -1) {
      return std::nullopt;
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
      case 0:
        return -1;
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
        return ControlChar::kBackspace;

      case 1:
        return ControlChar::kCtrlA;

      case 4:
        return ControlChar::kCtrlD;

      case 5:
        return ControlChar::kCtrlE;

      case 0x0b:
        return ControlChar::kCtrlK;

      case 0x0c:
        return ControlChar::kCtrlL;

      case 21:
        return ControlChar::kCtrlU;

      case 22:
        return ControlChar::kCtrlV;

      case 27: {
        int next = getch();
        // cerr << "Read next: " << next << "\n";
        switch (next) {
          case -1:
            return ControlChar::kEscape;

          case '[': {
            int next2 = getch();
            // cerr << "Read next2: " << next2 << "\n";
            switch (next2) {
              case 51:
                getch();
                return ControlChar::kDelete;
              case 53:
                getch();
                return ControlChar::kPageUp;
              case 54:
                getch();
                return ControlChar::kPageDown;
              case 'A':
                return ControlChar::kUpArrow;
              case 'B':
                return ControlChar::kDownArrow;
              case 'C':
                return ControlChar::kRightArrow;
              case 'D':
                return ControlChar::kLeftArrow;
              case 'F':
                return ControlChar::kEnd;
              case 'H':
                return ControlChar::kHome;
            }
          }
            return -1;
        }
        // std::cerr << "Unget: " << next << "\n";
        ungetch(next);
      }
        return ControlChar::kEscape;
      default:
        return output;
    }
  }
}

NonNull<std::unique_ptr<Screen>> NewScreenCurses() {
  return MakeNonNullUnique<ScreenCurses>();
}
}  // namespace afc::editor
