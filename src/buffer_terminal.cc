#include "src/buffer_terminal.h"

#include <cctype>
#include <ostream>

extern "C" {
#include <sys/ioctl.h>
}

#include "src/buffer.h"
#include "src/buffer_variables.h"
#include "src/char_buffer.h"
#include "src/editor.h"
#include "src/file_descriptor_reader.h"
#include "src/fuzz.h"
#include "src/lazy_string.h"
#include "src/safe_types.h"
#include "src/wstring.h"

namespace afc::editor {

BufferTerminal::BufferTerminal(OpenBuffer& buffer, BufferContents& contents)
    : data_(std::make_shared<Data>(
          Data{.buffer = buffer, .contents = contents})) {
  data_->buffer.view_size().Add(Observers::LockingObserver(
      std::weak_ptr<Data>(data_), InternalUpdateSize));

  LOG(INFO) << "New BufferTerminal for "
            << data_->buffer.Read(buffer_variables::name);
}

LineColumn BufferTerminal::position() const { return data_->position; }

void BufferTerminal::SetPosition(LineColumn position) {
  data_->position = position;
}

void BufferTerminal::ProcessCommandInput(
    shared_ptr<LazyString> str,
    const std::function<void()>& new_line_callback) {
  data_->position.line = min(data_->position.line, data_->buffer.EndLine());
  std::unordered_set<LineModifier, std::hash<int>> modifiers;

  ColumnNumber read_index;
  VLOG(5) << "Terminal input: " << str->ToString();
  auto follower = data_->buffer.GetEndPositionFollower();
  while (read_index < ColumnNumber(0) + str->size()) {
    int c = str->get(read_index);
    ++read_index;
    if (c == '\b') {
      VLOG(8) << "Received \\b";
      if (data_->position.column > ColumnNumber(0)) {
        data_->position.column--;
      }
    } else if (c == '\a') {
      VLOG(8) << "Received \\a";
      data_->buffer.status().Bell();
      audio::BeepFrequencies(
          data_->buffer.editor().audio_player(), 0.1,
          {audio::Frequency(783.99), audio::Frequency(523.25),
           audio::Frequency(659.25)});
    } else if (c == '\r') {
      VLOG(8) << "Received \\r";
      data_->position.column = ColumnNumber(0);
    } else if (c == '\n') {
      VLOG(8) << "Received \\n";
      new_line_callback();
      MoveToNextLine();
    } else if (c == 0x1b) {
      VLOG(8) << "Received 0x1b";
      read_index = ProcessTerminalEscapeSequence(str, read_index, &modifiers);
      VLOG(9) << "Modifiers: " << modifiers.size();
      CHECK_LE(data_->position.line, data_->buffer.EndLine());
    } else if (isprint(c) || c == '\t') {
      VLOG(8) << "Received printable or tab: " << c
              << " (modifiers: " << modifiers.size() << ", position "
              << data_->position << ")";
      if (data_->position.column >=
          ColumnNumber(0) + LastViewSize(*data_).column) {
        MoveToNextLine();
      }
      data_->contents.SetCharacter(data_->position, c, modifiers);
      data_->position.column++;
    } else {
      LOG(INFO) << "Unknown character: [" << c << "]\n";
    }
  }
}

std::vector<fuzz::Handler> BufferTerminal::FuzzHandlers() {
  using namespace fuzz;
  std::vector<Handler> output;
  output.push_back(Call(std::function<void()>([this]() { position(); })));

  output.push_back(Call(std::function<void(LineColumn)>(
      [this](LineColumn position) { SetPosition(position); })));

  output.push_back(Call(
      std::function<void(ShortRandomString)>([this](ShortRandomString input) {
        ProcessCommandInput(NewLazyString(std::move(input.value)),
                            []() { /* Nothing. */ });
      })));
  return output;
}

ColumnNumber BufferTerminal::ProcessTerminalEscapeSequence(
    shared_ptr<LazyString> str, ColumnNumber read_index,
    LineModifierSet* modifiers) {
  if (str->size() <= read_index.ToDelta()) {
    LOG(INFO) << "Unhandled character sequence: "
              << Substring(str, read_index)->ToString() << ")\n";
    return read_index;
  }
  switch (str->get(read_index)) {
    case 'M':
      VLOG(9) << "Received: cuu1: Up one line.";
      if (data_->position.line > LineNumber(0)) {
        --data_->position.line;
      }
      return read_index + ColumnNumberDelta(1);
    case '[':
      VLOG(9) << "Received: [";
      break;
    default:
      LOG(INFO) << "Unhandled character sequence: "
                << Substring(str, read_index)->ToString();
  }
  ++read_index;
  CHECK_LE(data_->position.line, data_->buffer.EndLine());
  auto current_line = data_->buffer.LineAt(data_->position.line);
  string sequence;
  while (read_index.ToDelta() < str->size()) {
    int c = str->get(read_index);
    ++read_index;
    switch (c) {
      case '@': {
        VLOG(9) << "Terminal: ich: Insert character.";
        data_->contents.InsertCharacter(data_->position);
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
        static const std::unordered_map<std::string, LineModifierSet> on{
            {"", {}},
            {"0", {}},
            {"0;30", {LineModifier::BLACK}},
            {"0;31", {LineModifier::RED}},
            {"0;32", {LineModifier::GREEN}},
            {"0;33", {LineModifier::YELLOW}},
            {"0;34", {LineModifier::BLUE}},
            {"0;35", {LineModifier::MAGENTA}},
            {"0;36", {LineModifier::CYAN}},
            {"1", {LineModifier::BOLD}},
            {"1;30", {LineModifier::BOLD, LineModifier::BLACK}},
            {"1;31", {LineModifier::BOLD, LineModifier::RED}},
            {"1;32", {LineModifier::BOLD, LineModifier::GREEN}},
            {"1;33", {LineModifier::BOLD, LineModifier::YELLOW}},
            {"1;34", {LineModifier::BOLD, LineModifier::BLUE}},
            {"1;35", {LineModifier::BOLD, LineModifier::MAGENTA}},
            {"1;36", {LineModifier::BOLD, LineModifier::CYAN}},
            // TODO(alejo): Support italic (3) on. "23" is Fraktur off, italic
            // off.
            {"3", {}},
            {"4", {LineModifier::UNDERLINE}},
            {"30", {LineModifier::BLACK}},
            {"31", {LineModifier::RED}},
            {"32", {LineModifier::GREEN}},
            {"33", {LineModifier::YELLOW}},
            {"34", {LineModifier::BLUE}},
            {"35", {LineModifier::MAGENTA}},
            {"36", {LineModifier::CYAN}},
        };
        static const std::unordered_map<std::string, LineModifierSet> off{
            {"24", {LineModifier::UNDERLINE}}};
        if (auto it = on.find(sequence); it != on.end()) {
          *modifiers = it->second;
        } else if (auto it = off.find(sequence); it != off.end()) {
          for (const auto& m : it->second) {
            modifiers->erase(m);
          }
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
        if (data_->position.column < current_line->EndColumn()) {
          auto follower = data_->buffer.GetEndPositionFollower();
          data_->position.column++;
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
            data_->buffer.status().SetWarningText(
                L"Unable to parse sequence from terminal in 'home' command: "
                L"\"" +
                FromByteString(sequence) + L"\"");
          }
          DLOG(INFO) << "Move cursor home: line: " << delta.line
                     << ", column: " << delta.column;
          data_->position = data_->buffer.editor()
                                .buffer_tree()
                                .GetActiveLeaf()
                                ->view_start() +
                            delta;
          auto follower = data_->buffer.GetEndPositionFollower();
          while (data_->position.line > data_->buffer.EndLine()) {
            data_->buffer.AppendEmptyLine();
          }
          follower = nullptr;
        }
        return read_index;

      case 'J':
        VLOG(9) << "Terminal: ed: clear to end of screen.";
        // Clears part of the screen.
        if (sequence == "" || sequence == "0") {
          VLOG(10) << "ed: Clear from cursor to end of screen.";
          data_->buffer.EraseLines(data_->position.line + LineNumberDelta(1),
                                   LineNumber(0) + data_->buffer.lines_size());
          data_->contents.DeleteToLineEnd(data_->position);
        } else if (sequence == "1") {
          VLOG(10) << "ed: Clear from cursor to beginning of the screen.";
          data_->buffer.EraseLines(LineNumber(0), data_->position.line);
          data_->contents.DeleteCharactersFromLine(
              LineColumn(), data_->position.column.ToDelta());
          data_->position = LineColumn();
        } else if (sequence == "2") {
          VLOG(10) << "ed: Clear entire screen (and moves cursor to upper left "
                      "on DOS ANSI.SYS).";
          data_->buffer.EraseLines(LineNumber(0),
                                   LineNumber(0) + data_->buffer.lines_size());
          data_->position = LineColumn();
        } else if (sequence == "3") {
          VLOG(10) << "ed: Clear entire screen and delete all lines saved in "
                      "the scrollback buffer (this feature was added for xterm "
                      "and is supported by other terminal applications).";
          data_->buffer.EraseLines(LineNumber(0),
                                   LineNumber(0) + data_->buffer.lines_size());
          data_->position = LineColumn();
        } else {
          VLOG(10) << "ed: Unknown sequence: " << sequence;
          data_->buffer.EraseLines(LineNumber(0),
                                   LineNumber(0) + data_->buffer.lines_size());
          data_->position = LineColumn();
        }
        CHECK_LE(data_->position.line, data_->buffer.EndLine());
        return read_index;

      case 'K': {
        VLOG(9) << "Terminal: el: clear to end of line.";
        data_->contents.DeleteToLineEnd(data_->position);
        return read_index;
      }

      case 'M':
        VLOG(9) << "Terminal: dl1: delete one line.";
        {
          data_->buffer.EraseLines(data_->position.line,
                                   data_->position.line + LineNumberDelta(1));
          CHECK_LE(data_->position.line, data_->buffer.EndLine());
        }
        return read_index;

      case 'P': {
        VLOG(9) << "Terminal: P";
        ColumnNumberDelta chars_to_erase(atoi(sequence.c_str()));
        ColumnNumber end_column =
            data_->contents.at(data_->position.line)->EndColumn();
        if (data_->position.column < end_column) {
          data_->contents.DeleteCharactersFromLine(
              data_->position,
              min(chars_to_erase, end_column - data_->position.column));
        }
        current_line = data_->buffer.LineAt(data_->position.line);
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
  auto follower = data_->buffer.GetEndPositionFollower();
  ++data_->position.line;
  data_->position.column = ColumnNumber(0);
  if (data_->position.line == LineNumber(0) + data_->buffer.lines_size()) {
    data_->buffer.AppendEmptyLine();
  }
}

void BufferTerminal::UpdateSize() { InternalUpdateSize(*data_); }

/* static */
void BufferTerminal::InternalUpdateSize(Data& data) {
  if (data.buffer.fd() == nullptr) {
    LOG(INFO) << "Buffer fd is nullptr!";
    return;
  }
  auto view_size = LastViewSize(data);
  if (data.last_updated_size.has_value() &&
      view_size == *data.last_updated_size) {
    return;
  }
  data.last_updated_size = view_size;
  struct winsize screen_size;
  LOG(INFO) << "Update buffer size: "
            << data.buffer.Read(buffer_variables::name) << " to: " << view_size;
  screen_size.ws_row = view_size.line.line_delta;
  screen_size.ws_col = view_size.column.column_delta;
  // Silence valgrind warnings about uninitialized values:
  screen_size.ws_xpixel = 0;
  screen_size.ws_ypixel = 0;
  CHECK(data.buffer.fd() != nullptr);

  if (ioctl(data.buffer.fd()->fd().read(), TIOCSWINSZ, &screen_size) == -1) {
    LOG(INFO) << "Buffer ioctl TICSWINSZ failed.";
    data.buffer.status().SetWarningText(L"ioctl TIOCSWINSZ failed: " +
                                        FromByteString(strerror(errno)));
  }
}

/* static */
LineColumnDelta BufferTerminal::LastViewSize(Data& data) {
  return data.buffer.view_size().Get().value_or(
      LineColumnDelta(LineNumberDelta(24), ColumnNumberDelta(80)));
}

}  // namespace afc::editor
