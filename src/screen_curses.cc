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
using afc::infrastructure::screen::Color;
using afc::infrastructure::screen::Screen;
using afc::infrastructure::screen::Style;
using afc::infrastructure::screen::StyleAttribute;
using afc::language::MakeNonNullUnique;
using afc::language::NonNull;
using afc::language::lazy_string::ColumnNumberDelta;
using afc::language::lazy_string::LazyString;
using afc::language::text::LineColumn;
using afc::language::text::LineColumnDelta;
using afc::language::text::LineNumberDelta;

namespace afc::editor {
namespace {
class ColorRegistry {
  std::map<std::pair<short, short>, int> pair_cache_;
  int next_pair_id_ = 1;

  short GetColorIndex(Color c) {
    const std::array<short, 8> kColorIndices = {
        COLOR_BLACK,    // 0
        COLOR_RED,      // 1
        COLOR_GREEN,    // 2
        COLOR_YELLOW,   // 3
        COLOR_BLUE,     // 4
        COLOR_MAGENTA,  // 5
        COLOR_CYAN,     // 6
        COLOR_WHITE     // 7
    };
    return kColorIndices[static_cast<size_t>(c)];
  }

 public:
  int GetPair(Color foreground, Color background) {
    short foreground_index = GetColorIndex(foreground);
    short background_index = GetColorIndex(background);
    auto key = std::make_pair(foreground_index, background_index);
    if (auto it = pair_cache_.find(key); it != pair_cache_.end())
      return it->second;

    LOG(INFO) << "Reserving pair for " << foreground_index << ", "
              << background_index << ": " << next_pair_id_;
    int id = next_pair_id_++;
    init_pair(id, foreground_index, background_index);
    pair_cache_[key] = id;
    return id;
  }
};

class ScreenCurses : public Screen {
  ColorRegistry color_registry_;

 public:
  ScreenCurses() {
    initscr();
    noecho();
    nodelay(stdscr, true);
    keypad(stdscr, false);
    start_color();
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

  void SetStyle(Style style) override {
    attr_t n_attrs = A_NORMAL;

    if (has_attribute(style.attributes, StyleAttribute::Bold))
      n_attrs |= A_BOLD;
    if (has_attribute(style.attributes, StyleAttribute::Italic))
      n_attrs |= A_ITALIC;
    if (has_attribute(style.attributes, StyleAttribute::Underline))
      n_attrs |= A_UNDERLINE;
    if (has_attribute(style.attributes, StyleAttribute::Dim)) n_attrs |= A_DIM;
    if (has_attribute(style.attributes, StyleAttribute::Reverse))
      n_attrs |= A_REVERSE;

    int pair_id =
        color_registry_.GetPair(style.foreground_color.value_or(Color::White),
                                style.background_color.value_or(Color::Black));
    attr_set(n_attrs, pair_id, nullptr);
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
        return ControlChar::Backspace;

      case 1:
        return ControlChar::CtrlA;

      case 4:
        return ControlChar::CtrlD;

      case 5:
        return ControlChar::CtrlE;

      case 0x0b:
        return ControlChar::CtrlK;

      case 0x0c:
        return ControlChar::CtrlL;

      case 21:
        return ControlChar::CtrlU;

      case 22:
        return ControlChar::CtrlV;

      case 27: {
        int next = getch();
        // cerr << "Read next: " << next << "\n";
        switch (next) {
          case -1:
            return ControlChar::Escape;

          case '[': {
            int next2 = getch();
            // cerr << "Read next2: " << next2 << "\n";
            switch (next2) {
              case 51:
                getch();
                return ControlChar::Delete;
              case 53:
                getch();
                return ControlChar::PageUp;
              case 54:
                getch();
                return ControlChar::PageDown;
              case 'A':
                return ControlChar::UpArrow;
              case 'B':
                return ControlChar::DownArrow;
              case 'C':
                return ControlChar::RightArrow;
              case 'D':
                return ControlChar::LeftArrow;
              case 'F':
                return ControlChar::End;
              case 'H':
                return ControlChar::Home;
            }
          }
            return -1;
        }
        // std::cerr << "Unget: " << next << "\n";
        ungetch(next);
      }
        return ControlChar::Escape;
      default:
        return output;
    }
  }
}

NonNull<std::unique_ptr<Screen>> NewScreenCurses() {
  return MakeNonNullUnique<ScreenCurses>();
}
}  // namespace afc::editor
