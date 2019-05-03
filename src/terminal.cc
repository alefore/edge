#include "src/terminal.h"

#include <glog/logging.h>

#include <algorithm>
#include <cctype>
#include <iostream>

#include "src/buffer_output_producer.h"
#include "src/buffer_variables.h"
#include "src/cursors_highlighter.h"
#include "src/delegating_output_receiver.h"
#include "src/delegating_output_receiver_with_internal_modifiers.h"
#include "src/dirname.h"
#include "src/frame_output_producer.h"
#include "src/horizontal_split_output_producer.h"
#include "src/line_marks.h"
#include "src/output_receiver.h"
#include "src/output_receiver_optimizer.h"
#include "src/parse_tree.h"
#include "src/screen_output_receiver.h"
#include "src/status_output_producer.h"

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
constexpr int Terminal::CTRL_A;
constexpr int Terminal::CTRL_D;
constexpr int Terminal::CTRL_E;
constexpr int Terminal::CTRL_L;
constexpr int Terminal::CTRL_U;
constexpr int Terminal::CTRL_K;

void Terminal::Display(EditorState* editor_state, Screen* screen,
                       const EditorState::ScreenState& screen_state) {
  if (screen_state.needs_hard_redraw) {
    screen->HardRefresh();
    editor_state->ScheduleRedraw();
  }
  if (screen_state.needs_redraw) {
    ShowBuffer(editor_state, screen);
  }
  ShowStatus(*editor_state, screen);
  auto buffer = editor_state->current_buffer();
  if (editor_state->status()->GetType() != Status::Type::kPrompt &&
      (buffer == nullptr || buffer->Read(buffer_variables::atomic_lines) ||
       !cursor_position_.has_value())) {
    screen->SetCursorVisibility(Screen::INVISIBLE);
  } else {
    screen->SetCursorVisibility(Screen::NORMAL);
    AdjustPosition(screen);
  }
  screen->Refresh();
  screen->Flush();
}

// Adjust the name of a buffer to a string suitable to be shown in the Status
// with progress indicators surrounding it.
//
// Empty strings -> "…"
// "$ xyz" -> "xyz"
// "$ abc/def/ghi" -> "ghi"
//
// The thinking is to return at most a single-character, and pick the most
// meaningful.
wstring TransformCommandNameForStatus(wstring name) {
  static const wstring kDefaultName = L"…";
  static const size_t kMaxLength = 5;

  size_t index = 0;
  if (name.size() > 2 && name[0] == L'$' && name[1] == L' ') {
    index = 2;
  }

  index = name.find_first_not_of(L' ', index);  // Skip spaces.
  if (index == string::npos) {
    return kDefaultName;
  }
  size_t end = name.find_first_of(L' ', index);
  wstring output = Basename(
      name.substr(index, end == string::npos ? string::npos : end - index));

  if (output.size() > kMaxLength) {
    output = output.substr(0, kMaxLength - kDefaultName.size()) + kDefaultName;
  }
  return output;
}

void Terminal::ShowStatus(const EditorState& editor_state, Screen* screen) {
  auto status = editor_state.status();
  if (status->text().empty()) {
    return;
  }

  screen->Move(LineNumber(0) + screen->lines() - LineNumberDelta(1),
               ColumnNumber(0));

  OutputProducer::Options options;
  options.receiver = std::make_unique<OutputReceiverOptimizer>(
      NewScreenOutputReceiver(screen));

  std::optional<ColumnNumber> active_cursor_column;
  options.active_cursor = &active_cursor_column;

  StatusOutputProducer(status, nullptr, editor_state.modifiers())
      .WriteLine(std::move(options));

  if (active_cursor_column.has_value()) {
    VLOG(5) << "Received cursor from status: " << active_cursor_column.value();
    cursor_position_ =
        LineColumn(LineNumber(0) + screen->lines() - LineNumberDelta(1),
                   active_cursor_column.value());
  }
};

void Terminal::ShowBuffer(EditorState* editor_state, Screen* screen) {
  auto status_lines = editor_state->status()->DesiredLines();

  screen->Move(LineNumber(0), ColumnNumber(0));

  cursor_position_ = std::nullopt;

  LineNumberDelta lines_to_show = screen->lines() - status_lines;
  auto buffer_tree = editor_state->buffer_tree();
  buffer_tree->SetSize(lines_to_show, screen->columns());
  auto output_producer = editor_state->buffer_tree()->CreateOutputProducer();
  for (auto line = LineNumber(0); line.ToDelta() < lines_to_show; ++line) {
    OutputProducer::Options options;
    options.receiver = std::make_unique<OutputReceiverOptimizer>(
        NewScreenOutputReceiver(screen));

    std::optional<ColumnNumber> active_cursor_column;
    options.active_cursor = &active_cursor_column;

    output_producer->WriteLine(std::move(options));

    if (active_cursor_column.has_value()) {
      VLOG(5) << "Received cursor from buffer: "
              << active_cursor_column.value();
      cursor_position_ = LineColumn(line, active_cursor_column.value());
    }
  }
}

void Terminal::AdjustPosition(Screen* screen) {
  CHECK(cursor_position_.has_value());
  VLOG(5) << "Setting cursor position: " << cursor_position_.value();
  screen->Move(cursor_position_.value().line, cursor_position_.value().column);
}

}  // namespace editor
}  // namespace afc
