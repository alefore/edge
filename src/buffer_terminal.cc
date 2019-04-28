#include "src/buffer_terminal.h"

#include <cctype>
#include <ostream>

extern "C" {
#include <sys/ioctl.h>
}

#include "src/buffer.h"
#include "src/editor.h"
#include "src/wstring.h"

namespace afc {
namespace editor {

BufferTerminal::BufferTerminal(OpenBuffer* buffer, BufferContents* contents)
    : buffer_(buffer), contents_(contents) {}

LineColumn BufferTerminal::position() const { return position_; }

void BufferTerminal::SetPosition(LineColumn position) { position_ = position; }

void BufferTerminal::SetSize(size_t lines, size_t columns) {
  if (lines_ == lines && columns_ == columns) {
    return;
  }
  struct winsize screen_size;
  lines_ = screen_size.ws_row = lines;
  columns_ = screen_size.ws_col = columns;
  if (ioctl(buffer_->fd(), TIOCSWINSZ, &screen_size) == -1) {
    buffer_->editor()->SetWarningStatus(L"ioctl TIOCSWINSZ failed: " +
                                        FromByteString(strerror(errno)));
  }
}

void BufferTerminal::ProcessCommandInput(shared_ptr<LazyString> str) {
  if (position_.line >= buffer_->lines_size()) {
    position_.line = buffer_->lines_size() - 1;
  }
  std::unordered_set<LineModifier, hash<int>> modifiers;

  size_t read_index = 0;
  VLOG(5) << "Terminal input: " << str->ToString();
  auto follower = buffer_->GetEndPositionFollower();
  while (read_index < str->size()) {
    int c = str->get(read_index);
    read_index++;
    if (c == '\b') {
      VLOG(8) << "Received \\b";
      if (position_.column > 0) {
        position_.column--;
      }
    } else if (c == '\a') {
      VLOG(8) << "Received \\a";
      auto status = buffer_->editor()->status();
      if (!all_of(status.begin(), status.end(), [](const wchar_t& c) {
            return c == L'♪' || c == L'♫' || c == L'…' || c == L' ' ||
                   c == L'𝄞';
          })) {
        status = L" 𝄞";
      } else if (status.size() >= 40) {
        status = L"…" + status.substr(status.size() - 40, status.size());
      }
      buffer_->SetStatus(status + L" " + (status.back() == L'♪' ? L"♫" : L"♪"));
      BeepFrequencies(buffer_->editor()->audio_player(),
                      {783.99, 523.25, 659.25});
    } else if (c == '\r') {
      VLOG(8) << "Received \\r";
      position_.column = 0;
    } else if (c == '\n') {
      VLOG(8) << "Received \\n";
      MoveToNextLine();
    } else if (c == 0x1b) {
      VLOG(8) << "Received 0x1b";
      read_index = ProcessTerminalEscapeSequence(str, read_index, &modifiers);
      CHECK_LT(position_.line, buffer_->lines_size());
    } else if (isprint(c) || c == '\t') {
      VLOG(8) << "Received printable or tab: " << c;
      if (position_.column >= columns_) {
        MoveToNextLine();
      }
      contents_->SetCharacter(position_.line, position_.column, c, modifiers);
      position_.column++;
    } else {
      LOG(INFO) << "Unknown character: [" << c << "]\n";
    }
  }
}

size_t BufferTerminal::ProcessTerminalEscapeSequence(
    shared_ptr<LazyString> str, size_t read_index,
    std::unordered_set<LineModifier, hash<int>>* modifiers) {
  if (str->size() <= read_index) {
    LOG(INFO) << "Unhandled character sequence: "
              << Substring(str, read_index)->ToString() << ")\n";
    return read_index;
  }
  switch (str->get(read_index)) {
    case 'M':
      VLOG(9) << "Received: cuu1: Up one line.";
      if (position_.line > 0) {
        position_.line--;
      }
      return read_index + 1;
    case '[':
      VLOG(9) << "Received: [";
      break;
    default:
      LOG(INFO) << "Unhandled character sequence: "
                << Substring(str, read_index)->ToString();
  }
  read_index++;
  CHECK_LT(position_.line, buffer_->lines_size());
  auto current_line = buffer_->LineAt(position_.line);
  string sequence;
  while (read_index < str->size()) {
    int c = str->get(read_index);
    read_index++;
    switch (c) {
      case '@': {
        VLOG(9) << "Terminal: ich: Insert character.";
        contents_->InsertCharacter(position_.line, position_.column);
        return read_index;
      }

      case 'l':
        VLOG(9) << "Terminal: l";
        if (sequence == "?1") {
          VLOG(9) << "Terminal: ?1";
          sequence.push_back(c);
          continue;
        }
        if (sequence == "?1049") {
          VLOG(9) << "Terminal: ?1049: rmcup";
        } else if (sequence == "?25") {
          LOG(INFO) << "Ignoring: Make cursor invisible";
        } else {
          LOG(INFO) << "Unhandled character sequence: " << sequence;
        }
        return read_index;

      case 'h':
        VLOG(9) << "Terminal: h";
        if (sequence == "?1") {
          sequence.push_back(c);
          continue;
        }
        if (sequence == "?1049") {
          // smcup
        } else if (sequence == "?25") {
          LOG(INFO) << "Ignoring: Make cursor visible";
        } else {
          LOG(INFO) << "Unhandled character sequence: " << sequence;
        }
        return read_index;

      case 'm':
        VLOG(9) << "Terminal: m";
        if (sequence == "") {
          modifiers->clear();
        } else if (sequence == "0") {
          modifiers->clear();
        } else if (sequence == "1") {
          modifiers->insert(LineModifier::BOLD);
        } else if (sequence == "3") {
          // TODO(alejo): Support italic on.
        } else if (sequence == "4") {
          modifiers->insert(LineModifier::UNDERLINE);
        } else if (sequence == "23") {
          // Fraktur off, italic off.  No need to do anything for now.
        } else if (sequence == "24") {
          modifiers->erase(LineModifier::UNDERLINE);
        } else if (sequence == "31") {
          modifiers->clear();
          modifiers->insert(LineModifier::RED);
        } else if (sequence == "32") {
          modifiers->clear();
          modifiers->insert(LineModifier::GREEN);
        } else if (sequence == "36") {
          modifiers->clear();
          modifiers->insert(LineModifier::CYAN);
        } else if (sequence == "1;30") {
          modifiers->clear();
          modifiers->insert(LineModifier::BOLD);
          modifiers->insert(LineModifier::BLACK);
        } else if (sequence == "1;31") {
          modifiers->clear();
          modifiers->insert(LineModifier::BOLD);
          modifiers->insert(LineModifier::RED);
        } else if (sequence == "1;36") {
          modifiers->clear();
          modifiers->insert(LineModifier::BOLD);
          modifiers->insert(LineModifier::CYAN);
        } else if (sequence == "0;36") {
          modifiers->clear();
          modifiers->insert(LineModifier::CYAN);
        } else {
          LOG(INFO) << "Unhandled character sequence: (" << sequence;
        }
        return read_index;

      case '>':
        VLOG(9) << "Terminal: >";
        if (sequence == "?1l\E") {
          // rmkx: leave 'keyboard_transmit' mode
          // TODO(alejo): Handle it.
        } else {
          LOG(INFO) << "Unhandled character sequence: " << sequence;
        }
        return read_index;
        break;

      case '=':
        VLOG(9) << "Terminal: =";
        if (sequence == "?1h\E") {
          // smkx: enter 'keyboard_transmit' mode
          // TODO(alejo): Handle it.
        } else {
          LOG(INFO) << "Unhandled character sequence: " << sequence;
        }
        return read_index;
        break;

      case 'C':
        VLOG(9) << "Terminal: cuf1: non-destructive space (move right 1 space)";
        if (position_.column < current_line->size()) {
          auto follower = buffer_->GetEndPositionFollower();
          position_.column++;
        }
        return read_index;

      case 'H':
        VLOG(9) << "Terminal: home: move cursor home.";
        {
          LinesDelta lines_delta;
          ColumnsDelta columns_delta;
          size_t pos = sequence.find(';');
          try {
            if (pos != wstring::npos) {
              lines_delta =
                  LinesDelta(pos == 0 ? 0 : stoul(sequence.substr(0, pos)) - 1);
              columns_delta =
                  ColumnsDelta(pos == sequence.size() - 1
                                   ? 0
                                   : stoul(sequence.substr(pos + 1)) - 1);
            } else if (!sequence.empty()) {
              lines_delta = LinesDelta(stoul(sequence));
            }
          } catch (const std::invalid_argument& ia) {
            buffer_->SetStatus(
                L"Unable to parse sequence from terminal in 'home' command: "
                L"\"" +
                FromByteString(sequence) + L"\"");
          }
          DLOG(INFO) << "Move cursor home: line: " << lines_delta.delta
                     << ", column: " << columns_delta.delta;
          position_ =
              buffer_->editor()->buffer_tree()->GetActiveLeaf()->view_start() +
              lines_delta + columns_delta;
          auto follower = buffer_->GetEndPositionFollower();
          while (position_.line >= buffer_->lines_size()) {
            buffer_->AppendEmptyLine();
          }
          follower = nullptr;
        }
        return read_index;

      case 'J':
        VLOG(9) << "Terminal: ed: clear to end of screen.";
        // Clears part of the screen.
        if (sequence == "" || sequence == "0") {
          VLOG(10) << "ed: Clear from cursor to end of screen.";
          buffer_->EraseLines(position_.line + 1, buffer_->lines_size());
          contents_->DeleteCharactersFromLine(position_.line, position_.column);
        } else if (sequence == "1") {
          VLOG(10) << "ed: Clear from cursor to beginning of the screen.";
          buffer_->EraseLines(0, position_.line);
          contents_->DeleteCharactersFromLine(0, 0, position_.column);
          position_ = LineColumn();
        } else if (sequence == "2") {
          VLOG(10) << "ed: Clear entire screen (and moves cursor to upper left "
                      "on DOS ANSI.SYS).";
          buffer_->EraseLines(0, buffer_->lines_size());
          position_ = LineColumn();
        } else if (sequence == "3") {
          VLOG(10) << "ed: Clear entire screen and delete all lines saved in "
                      "the scrollback buffer (this feature was added for xterm "
                      "and is supported by other terminal applications).";
          buffer_->EraseLines(0, buffer_->lines_size());
          position_ = LineColumn();
        } else {
          VLOG(10) << "ed: Unknown sequence: " << sequence;
          buffer_->EraseLines(0, buffer_->lines_size());
          position_ = LineColumn();
        }
        CHECK_LT(position_.line, buffer_->lines_size());
        return read_index;

      case 'K': {
        VLOG(9) << "Terminal: el: clear to end of line.";
        contents_->DeleteCharactersFromLine(position_.line, position_.column);
        return read_index;
      }

      case 'M':
        VLOG(9) << "Terminal: dl1: delete one line.";
        {
          buffer_->EraseLines(position_.line, position_.line + 1);
          CHECK_LT(position_.line, buffer_->lines_size());
        }
        return read_index;

      case 'P': {
        VLOG(9) << "Terminal: P";
        size_t chars_to_erase = static_cast<size_t>(atoi(sequence.c_str()));
        size_t length = contents_->at(position_.line)->size();
        if (position_.column < length) {
          contents_->DeleteCharactersFromLine(
              position_.line, position_.column,
              min(chars_to_erase, length - position_.column));
        }
        current_line = buffer_->LineAt(position_.line);
        return read_index;
      }
      default:
        sequence.push_back(c);
    }
  }
  LOG(INFO) << "Unhandled character sequence: " << sequence;
  return read_index;
}

void BufferTerminal::MoveToNextLine() {
  auto follower = buffer_->GetEndPositionFollower();
  position_.line++;
  position_.column = 0;
  if (position_.line == buffer_->lines_size()) {
    buffer_->AppendEmptyLine();
  }
}

}  // namespace editor
}  // namespace afc
