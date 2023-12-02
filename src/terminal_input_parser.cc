#include "src/terminal_input_parser.h"

#include <cctype>
#include <csignal>
#include <ostream>
#include <vector>

extern "C" {
#include <sys/ioctl.h>
}

#include "src/buffer_name.h"
#include "src/language/lazy_string/char_buffer.h"
#include "src/language/lazy_string/lazy_string.h"
#include "src/language/lazy_string/substring.h"
#include "src/language/safe_types.h"
#include "src/language/text/line.h"
#include "src/language/text/line_sequence.h"
#include "src/language/text/mutable_line_sequence.h"
#include "src/language/wstring.h"
#include "src/tests/fuzz.h"

using afc::infrastructure::UnixSignal;
using afc::infrastructure::screen::LineModifier;
using afc::infrastructure::screen::LineModifierSet;
using afc::language::EmptyValue;
using afc::language::Error;
using afc::language::FromByteString;
using afc::language::MakeNonNullShared;
using afc::language::NonNull;
using afc::language::Observers;
using afc::language::lazy_string::ColumnNumber;
using afc::language::lazy_string::ColumnNumberDelta;
using afc::language::lazy_string::LazyString;
using afc::language::lazy_string::NewLazyString;
using afc::language::text::Line;
using afc::language::text::LineBuilder;
using afc::language::text::LineColumn;
using afc::language::text::LineColumnDelta;
using afc::language::text::LineNumber;
using afc::language::text::LineNumberDelta;
using afc::language::text::MutableLineSequence;

namespace afc::editor {

TerminalAdapter ::TerminalAdapter(
    NonNull<std::unique_ptr<TerminalAdapter::Receiver>> receiver,
    MutableLineSequence& contents)
    : data_(MakeNonNullShared<Data>(
          Data{.receiver = std::move(receiver), .contents = contents})) {
  data_->receiver->view_size().Add(Observers::LockingObserver(
      std::weak_ptr<Data>(data_.get_shared()), InternalUpdateSize));

  LOG(INFO) << "New TerminalAdapter for " << data_->receiver->name();
}

std::optional<LineColumn> TerminalAdapter::position() const {
  return data_->position;
}

void TerminalAdapter::SetPositionToZero() { data_->position = LineColumn(); }

futures::Value<EmptyValue> TerminalAdapter::ReceiveInput(
    NonNull<std::shared_ptr<LazyString>> str,
    const LineModifierSet& initial_modifiers,
    const std::function<void(LineNumberDelta)>& new_line_callback) {
  data_->position.line =
      std::min(data_->position.line, data_->receiver->contents().EndLine());
  LineModifierSet modifiers = initial_modifiers;

  ColumnNumber read_index;
  VLOG(5) << "Terminal input: " << str->ToString();
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
      data_->receiver->Bell();
    } else if (c == '\r') {
      VLOG(8) << "Received \\r";
      data_->position.column = ColumnNumber(0);
    } else if (c == '\n') {
      VLOG(8) << "Received \\n";
      new_line_callback(LineNumberDelta(1));
      MoveToNextLine();
    } else if (c == 0x1b) {
      VLOG(8) << "Received 0x1b";
      read_index = ProcessTerminalEscapeSequence(str, read_index, &modifiers);
      VLOG(9) << "Modifiers: " << modifiers.size();
      CHECK_LE(data_->position.line, data_->receiver->contents().EndLine());
    } else if (isprint(c) || c == '\t') {
      VLOG(8) << "Received printable or tab: " << c
              << " (modifiers: " << modifiers.size() << ", position "
              << data_->position << ")";
      if (data_->position.column >=
          ColumnNumber(0) + LastViewSize(data_.value()).column) {
        MoveToNextLine();
      }
      data_->contents.SetCharacter(data_->position, c, modifiers);
      data_->position.column++;
    } else {
      LOG(INFO) << "Unknown character: [" << c << "]\n";
    }
  }
  data_->receiver->JumpToPosition(data_->position);
  return futures::Past(EmptyValue());
}

std::vector<tests::fuzz::Handler> TerminalAdapter::FuzzHandlers() {
  using namespace tests::fuzz;
  std::vector<Handler> output;
  output.push_back(Call(std::function<void()>([this]() { position(); })));

  output.push_back(
      Call(std::function<void()>([this]() { SetPositionToZero(); })));

  output.push_back(Call(
      std::function<void(ShortRandomString)>([this](ShortRandomString input) {
        return ReceiveInput(NewLazyString(std::move(input.value)), {},
                            [](LineNumberDelta) {});
      })));
  return output;
}

ColumnNumber TerminalAdapter::ProcessTerminalEscapeSequence(
    NonNull<std::shared_ptr<LazyString>> str, ColumnNumber read_index,
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
  CHECK_LE(data_->position.line, data_->receiver->contents().EndLine());
  auto current_line = data_->receiver->contents().at(data_->position.line);
  std::string sequence;
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
            {"0;30", {LineModifier::kBlack}},
            {"0;31", {LineModifier::kRed}},
            {"0;32", {LineModifier::kGreen}},
            {"0;33", {LineModifier::kYellow}},
            {"0;34", {LineModifier::kBlue}},
            {"0;35", {LineModifier::kMagenta}},
            {"0;36", {LineModifier::kCyan}},
            {"1", {LineModifier::kBold}},
            {"1;30", {LineModifier::kBold, LineModifier::kBlack}},
            {"1;31", {LineModifier::kBold, LineModifier::kRed}},
            {"1;32", {LineModifier::kBold, LineModifier::kGreen}},
            {"1;33", {LineModifier::kBold, LineModifier::kYellow}},
            {"1;34", {LineModifier::kBold, LineModifier::kBlue}},
            {"1;35", {LineModifier::kBold, LineModifier::kMagenta}},
            {"1;36", {LineModifier::kBold, LineModifier::kCyan}},
            // TODO(alejo): Support italic (3) on. "23" is Fraktur off, italic
            // off.
            {"3", {}},
            {"4", {LineModifier::kUnderline}},
            {"30", {LineModifier::kBlack}},
            {"31", {LineModifier::kRed}},
            {"32", {LineModifier::kGreen}},
            {"33", {LineModifier::kYellow}},
            {"34", {LineModifier::kBlue}},
            {"35", {LineModifier::kMagenta}},
            {"36", {LineModifier::kCyan}},
        };
        static const std::unordered_map<std::string, LineModifierSet> off{
            {"24", {LineModifier::kUnderline}}};
        if (auto it_on = on.find(sequence); it_on != on.end()) {
          *modifiers = it_on->second;
        } else if (auto it_off = off.find(sequence); it_off != off.end()) {
          for (const auto& m : it_off->second) {
            modifiers->erase(m);
          }
        } else if (sequence == "0;36") {
          modifiers->clear();
          modifiers->insert(LineModifier::kCyan);
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
          data_->position.column++;
          data_->receiver->JumpToPosition(data_->position);
        }
        return read_index;

      case 'H':
        VLOG(9) << "Terminal: home: move cursor home.";
        {
          LineColumnDelta delta;
          size_t pos = sequence.find(';');
          try {
            if (pos != std::wstring::npos) {
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
            data_->receiver->Warn(Error(
                L"Unable to parse sequence from terminal in 'home' command: "
                L"\"" +
                FromByteString(sequence) + L"\""));
          }
          DLOG(INFO) << "Move cursor home: line: " << delta.line
                     << ", column: " << delta.column;
          data_->position =
              data_->receiver->current_widget_view_start() + delta;
          while (data_->position.line > data_->receiver->contents().EndLine()) {
            data_->receiver->AppendEmptyLine();
          }
          data_->receiver->JumpToPosition(data_->position);
        }
        return read_index;

      case 'J':
        VLOG(9) << "Terminal: ed: clear to end of screen.";
        // Clears part of the screen.
        if (sequence == "" || sequence == "0") {
          VLOG(10) << "ed: Clear from cursor to end of screen.";
          data_->receiver->EraseLines(
              data_->position.line + LineNumberDelta(1),
              LineNumber(0) + data_->receiver->contents().size());
          data_->contents.DeleteToLineEnd(data_->position);
        } else if (sequence == "1") {
          VLOG(10) << "ed: Clear from cursor to beginning of the screen.";
          data_->receiver->EraseLines(LineNumber(0), data_->position.line);
          data_->contents.DeleteCharactersFromLine(
              LineColumn(), data_->position.column.ToDelta());
          data_->position = LineColumn();
        } else if (sequence == "2") {
          VLOG(10) << "ed: Clear entire screen (and moves cursor to upper left "
                      "on DOS ANSI.SYS).";
          data_->receiver->EraseLines(
              LineNumber(0),
              LineNumber(0) + data_->receiver->contents().size());
          data_->position = LineColumn();
        } else if (sequence == "3") {
          VLOG(10) << "ed: Clear entire screen and delete all lines saved in "
                      "the scrollback buffer (this feature was added for xterm "
                      "and is supported by other terminal applications).";
          data_->receiver->EraseLines(
              LineNumber(0),
              LineNumber(0) + data_->receiver->contents().size());
          data_->position = LineColumn();
        } else {
          VLOG(10) << "ed: Unknown sequence: " << sequence;
          data_->receiver->EraseLines(
              LineNumber(0),
              LineNumber(0) + data_->receiver->contents().size());
          data_->position = LineColumn();
        }
        CHECK_LE(data_->position.line, data_->receiver->contents().EndLine());
        return read_index;

      case 'K': {
        VLOG(9) << "Terminal: el: clear to end of line.";
        data_->contents.DeleteToLineEnd(data_->position);
        return read_index;
      }

      case 'M':
        VLOG(9) << "Terminal: dl1: delete one line.";
        {
          data_->receiver->EraseLines(
              data_->position.line, data_->position.line + LineNumberDelta(1));
          CHECK_LE(data_->position.line, data_->receiver->contents().EndLine());
        }
        return read_index;

      case 'P': {
        VLOG(9) << "Terminal: P";
        ColumnNumberDelta chars_to_erase(atoi(sequence.c_str()));
        ColumnNumber end_column =
            data_->receiver->contents().at(data_->position.line)->EndColumn();
        if (data_->position.column < end_column) {
          data_->contents.DeleteCharactersFromLine(
              data_->position,
              std::min(chars_to_erase, end_column - data_->position.column));
        }
        current_line = data_->receiver->contents().at(data_->position.line);
        return read_index;
      }
      default:
        sequence.push_back(c);
    }
  }
  LOG(INFO) << "Unhandled character sequence: " << sequence;
  return read_index;
}

void TerminalAdapter::MoveToNextLine() {
  ++data_->position.line;
  data_->position.column = ColumnNumber(0);
  if (data_->position.line ==
      LineNumber(0) + data_->receiver->contents().size()) {
    data_->receiver->AppendEmptyLine();
  }
  data_->receiver->JumpToPosition(data_->position);
}

void TerminalAdapter::UpdateSize() { InternalUpdateSize(data_.value()); }

/* static */
void TerminalAdapter::InternalUpdateSize(Data& data) {
  std::optional<infrastructure::FileDescriptor> fd = data.receiver->fd();
  if (fd == std::nullopt) {
    LOG(INFO) << "Buffer fd is gone.";
    return;
  }
  auto view_size = LastViewSize(data);
  if (data.last_updated_size.has_value() &&
      view_size == *data.last_updated_size) {
    return;
  }
  data.last_updated_size = view_size;
  struct winsize screen_size;
  LOG(INFO) << "Update buffer size: " << data.receiver->name()
            << " to: " << view_size;
  screen_size.ws_row = view_size.line.read();
  screen_size.ws_col = view_size.column.read();
  // Silence valgrind warnings about uninitialized values:
  screen_size.ws_xpixel = 0;
  screen_size.ws_ypixel = 0;

  if (ioctl(fd->read(), TIOCSWINSZ, &screen_size) == -1) {
    LOG(INFO) << "Buffer ioctl TICSWINSZ failed.";
    data.receiver->Warn(
        Error(L"ioctl TIOCSWINSZ failed: " + FromByteString(strerror(errno))));
  }
}

/* static */
LineColumnDelta TerminalAdapter::LastViewSize(Data& data) {
  return data.receiver->view_size().Get().value_or(
      LineColumnDelta(LineNumberDelta(24), ColumnNumberDelta(80)));
}

bool TerminalAdapter::WriteSignal(UnixSignal signal) {
  if (std::optional<infrastructure::FileDescriptor> fd = data_->receiver->fd();
      fd != std::nullopt)
    switch (signal.read()) {
      case SIGINT: {
        std::string sequence(1, 0x03);
        (void)write(fd->read(), sequence.c_str(), sequence.size());
        return true;
      }

      case SIGTSTP: {
        static const std::string sequence(1, 0x1a);
        (void)write(fd->read(), sequence.c_str(), sequence.size());
        return true;
      }
    }
  return false;
}
}  // namespace afc::editor
