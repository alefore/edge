#include "src/buffer_terminal.h"

#include <cctype>
#include <ostream>

extern "C" {
#include <sys/ioctl.h>
}

#include "src/buffer.h"
#include "src/editor.h"
#include "src/file_descriptor_reader.h"
#include "src/wstring.h"

namespace afc {
namespace editor {

BufferTerminal::BufferTerminal(OpenBuffer* buffer, BufferContents* contents)
    : buffer_(buffer), contents_(contents) {}

LineColumn BufferTerminal::position() const { return position_; }

void BufferTerminal::SetPosition(LineColumn position) { position_ = position; }

void BufferTerminal::SetSize(LineNumberDelta lines, ColumnNumberDelta columns) {
  if (lines_ == lines && columns_ == columns) {
    return;
  }
  struct winsize screen_size;
  lines_ = lines;
  columns_ = columns;
  screen_size.ws_row = lines.line_delta;
  screen_size.ws_col = columns.column_delta;
  if (buffer_->fd() != nullptr &&
      ioctl(buffer_->fd()->fd(), TIOCSWINSZ, &screen_size) == -1) {
    buffer_->status()->SetWarningText(L"ioctl TIOCSWINSZ failed: " +
                                      FromByteString(strerror(errno)));
  }
}

void BufferTerminal::ProcessCommandInput(
    shared_ptr<LazyString> str,
    const std::function<void()>& new_line_callback) {
  position_.line = min(position_.line, buffer_->EndLine());
  std::unordered_set<LineModifier, hash<int>> modifiers;

  size_t read_index = 0;
  VLOG(5) << "Terminal input: " << str->ToString();
  auto follower = buffer_->GetEndPositionFollower();
  while (read_index < str->size()) {
    int c = str->get(read_index);
    read_index++;
    if (c == '\b') {
      VLOG(8) << "Received \\b";
      if (position_.column > ColumnNumber(0)) {
        position_.column--;
      }
    } else if (c == '\a') {
      VLOG(8) << "Received \\a";
      buffer_->status()->Bell();
      BeepFrequencies(buffer_->editor()->audio_player(),
                      {783.99, 523.25, 659.25});
    } else if (c == '\r') {
      VLOG(8) << "Received \\r";
      position_.column = ColumnNumber(0);
    } else if (c == '\n') {
      VLOG(8) << "Received \\n";
      new_line_callback();
      MoveToNextLine();
    } else if (c == 0x1b) {
      VLOG(8) << "Received 0x1b";
      read_index = ProcessTerminalEscapeSequence(str, read_index, &modifiers);
      CHECK_LE(position_.line, buffer_->EndLine());
    } else if (isprint(c) || c == '\t') {
      VLOG(8) << "Received printable or tab: " << c;
      if (position_.column >= ColumnNumber(0) + columns_) {
        MoveToNextLine();
      }
      contents_->SetCharacter(position_.line, position_.column, c, modifiers);
      position_.column++;
    } else {
      LOG(INFO) << "Unknown character: [" << c << "]\n";
    }
  }
}

// TODO: Turn the type of read_index into ColumnNumber.
size_t BufferTerminal::ProcessTerminalEscapeSequence(
    shared_ptr<LazyString> str, size_t read_index,
    std::unordered_set<LineModifier, hash<int>>* modifiers) {
  if (str->size() <= read_index) {
    LOG(INFO) << "Unhandled character sequence: "
              << Substring(str, ColumnNumber(read_index))->ToString() << ")\n";
    return read_index;
  }
  switch (str->get(read_index)) {
    case 'M':
      VLOG(9) << "Received: cuu1: Up one line.";
      if (position_.line > LineNumber(0)) {
        --position_.line;
      }
      return read_index + 1;
    case '[':
      VLOG(9) << "Received: [";
      break;
    default:
      LOG(INFO) << "Unhandled character sequence: "
                << Substring(str, ColumnNumber(read_index))->ToString();
  }
  read_index++;
  CHECK_LE(position_.line, buffer_->EndLine());
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
        if (position_.column < current_line->EndColumn()) {
          auto follower = buffer_->GetEndPositionFollower();
          position_.column++;
        }
        return read_index;

      case 'H':
        VLOG(9) << "Terminal: home: move cursor home.";
        {
          LineColumnDelta delta;
          size_t pos = sequence.find(';');
          try {
            if (pos != wstring::npos) {
              delta.line = LineNumberDelta(
                  pos == 0 ? 0 : stoul(sequence.substr(0, pos)) - 1);
              delta.column =
                  ColumnNumberDelta(pos == sequence.size() - 1
                                        ? 0
                                        : stoul(sequence.substr(pos + 1)) - 1);
            } else if (!sequence.empty()) {
              delta.line = LineNumberDelta(stoul(sequence));
            }
          } catch (const std::invalid_argument& ia) {
            buffer_->status()->SetWarningText(
                L"Unable to parse sequence from terminal in 'home' command: "
                L"\"" +
                FromByteString(sequence) + L"\"");
          }
          DLOG(INFO) << "Move cursor home: line: " << delta.line
                     << ", column: " << delta.column;
          position_ =
              buffer_->editor()->buffer_tree()->GetActiveLeaf()->view_start() +
              delta;
          auto follower = buffer_->GetEndPositionFollower();
          while (position_.line > buffer_->EndLine()) {
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
          buffer_->EraseLines(position_.line + LineNumberDelta(1),
                              LineNumber(0) + buffer_->lines_size());
          contents_->DeleteCharactersFromLine(position_.line, position_.column);
        } else if (sequence == "1") {
          VLOG(10) << "ed: Clear from cursor to beginning of the screen.";
          buffer_->EraseLines(LineNumber(0), position_.line);
          contents_->DeleteCharactersFromLine(LineNumber(0), ColumnNumber(0),
                                              position_.column.ToDelta());
          position_ = LineColumn();
        } else if (sequence == "2") {
          VLOG(10) << "ed: Clear entire screen (and moves cursor to upper left "
                      "on DOS ANSI.SYS).";
          buffer_->EraseLines(LineNumber(0),
                              LineNumber(0) + buffer_->lines_size());
          position_ = LineColumn();
        } else if (sequence == "3") {
          VLOG(10) << "ed: Clear entire screen and delete all lines saved in "
                      "the scrollback buffer (this feature was added for xterm "
                      "and is supported by other terminal applications).";
          buffer_->EraseLines(LineNumber(0),
                              LineNumber(0) + buffer_->lines_size());
          position_ = LineColumn();
        } else {
          VLOG(10) << "ed: Unknown sequence: " << sequence;
          buffer_->EraseLines(LineNumber(0),
                              LineNumber(0) + buffer_->lines_size());
          position_ = LineColumn();
        }
        CHECK_LE(position_.line, buffer_->EndLine());
        return read_index;

      case 'K': {
        VLOG(9) << "Terminal: el: clear to end of line.";
        contents_->DeleteCharactersFromLine(position_.line, position_.column);
        return read_index;
      }

      case 'M':
        VLOG(9) << "Terminal: dl1: delete one line.";
        {
          buffer_->EraseLines(position_.line,
                              position_.line + LineNumberDelta(1));
          CHECK_LE(position_.line, buffer_->EndLine());
        }
        return read_index;

      case 'P': {
        VLOG(9) << "Terminal: P";
        ColumnNumberDelta chars_to_erase(atoi(sequence.c_str()));
        ColumnNumber end_column = contents_->at(position_.line)->EndColumn();
        if (position_.column < end_column) {
          contents_->DeleteCharactersFromLine(
              position_.line, position_.column,
              min(chars_to_erase, end_column - position_.column));
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
  ++position_.line;
  position_.column = ColumnNumber(0);
  if (position_.line == LineNumber(0) + buffer_->lines_size()) {
    buffer_->AppendEmptyLine();
  }
}

}  // namespace editor
}  // namespace afc
