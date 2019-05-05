#include "src/terminal.h"

#include <glog/logging.h>

#include <algorithm>
#include <cctype>
#include <iostream>

#include "src/buffer_output_producer.h"
#include "src/buffer_variables.h"
#include "src/dirname.h"
#include "src/frame_output_producer.h"
#include "src/horizontal_split_output_producer.h"
#include "src/line_marks.h"
#include "src/parse_tree.h"
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
    hashes_current_lines_.empty();
    editor_state->ScheduleRedraw();
  }
  ShowBuffer(editor_state, screen);
  ShowStatus(*editor_state, screen);
  auto buffer = editor_state->current_buffer();
  if (editor_state->status()->GetType() == Status::Type::kPrompt ||
      (buffer != nullptr &&
       buffer->status()->GetType() == Status::Type::kPrompt) ||
      (buffer != nullptr && !buffer->Read(buffer_variables::atomic_lines) &&
       cursor_position_.has_value())) {
    screen->SetCursorVisibility(Screen::NORMAL);
    AdjustPosition(screen);
  } else {
    screen->SetCursorVisibility(Screen::INVISIBLE);
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

  auto line = LineNumber(0) + screen->lines() - LineNumberDelta(1);
  screen->Move(line, ColumnNumber(0));

  WriteLine(
      screen, line,
      StatusOutputProducer(status, nullptr, editor_state.modifiers()).Next());
};

void Terminal::ShowBuffer(EditorState* editor_state, Screen* screen) {
  auto status_lines = editor_state->status()->DesiredLines();

  screen->Move(LineNumber(0), ColumnNumber(0));

  LineNumberDelta lines_to_show = screen->lines() - status_lines;
  auto buffer_tree = editor_state->buffer_tree();
  buffer_tree->SetSize(lines_to_show, screen->columns());
  auto output_producer = editor_state->buffer_tree()->CreateOutputProducer();
  for (auto line = LineNumber(0); line.ToDelta() < lines_to_show; ++line) {
    WriteLine(screen, line, output_producer->Next());
  }
}

void FlushModifiers(Screen* screen, const LineModifierSet& modifiers) {
  screen->SetModifier(LineModifier::RESET);
  for (const auto& m : modifiers) {
    screen->SetModifier(m);
  }
}

void Terminal::WriteLine(Screen* screen, LineNumber line,
                         OutputProducer::Generator generator) {
  if (generator.inputs_hash.has_value()) {
    VLOG(9) << "Checking line " << line << " with hash "
            << generator.inputs_hash.value();
    if (line.line < hashes_current_lines_.size() &&
        hashes_current_lines_[line.line] == generator.inputs_hash.value()) {
      VLOG(5) << "Skipping unnecessary render for " << line;
      return;
    }
  }
  if (hashes_current_lines_.size() <= line.line) {
    hashes_current_lines_.resize(line.line * 2 + 50);
  }
  hashes_current_lines_[line.line] = generator.inputs_hash;

  screen->Move(line, ColumnNumber(0));

  VLOG(8) << "Generating line for screen " << line;
  auto line_with_cursor = generator.generate();
  CHECK(line_with_cursor.line != nullptr);
  VLOG(6) << "Writing line of length: "
          << line_with_cursor.line->EndColumn().ToDelta();
  ColumnNumber input_column;
  ColumnNumber output_column;

  if (line_with_cursor.cursor.has_value()) {
    cursor_position_ = std::nullopt;
  }

  screen->SetModifier(LineModifier::RESET);
  auto modifiers_it =
      line_with_cursor.line->modifiers().lower_bound(input_column);
  auto width = screen->columns();

  while (input_column < line_with_cursor.line->EndColumn() &&
         output_column < ColumnNumber(0) + width) {
    if (line_with_cursor.cursor.has_value() &&
        input_column == line_with_cursor.cursor.value()) {
      cursor_position_ = LineColumn(line, output_column);
    }

    // Each iteration will advance input_column and then print between start and
    // input_column.
    auto start = input_column;
    while ((input_column < line_with_cursor.line->EndColumn() &&
            output_column < ColumnNumber(0) + width &&
            (!line_with_cursor.cursor.has_value() ||
             input_column != line_with_cursor.cursor.value() ||
             cursor_position_ == LineColumn(line, output_column)) &&
            (modifiers_it == line_with_cursor.line->modifiers().end() ||
             modifiers_it->first > input_column))) {
      output_column += ColumnNumberDelta(
          wcwidth(line_with_cursor.line->contents()->get(input_column.column)));
      ++input_column;
    }

    // TODO: Have screen receive the LazyString directly.
    if (start != input_column) {
      screen->WriteString(Substring(line_with_cursor.line->contents(), start,
                                    input_column - start)
                              ->ToString());
    }

    if (modifiers_it != line_with_cursor.line->modifiers().end()) {
      CHECK_GE(modifiers_it->first, input_column);
      if (modifiers_it->first == input_column) {
        FlushModifiers(screen, modifiers_it->second);
        ++modifiers_it;
      }
    }
  }

  if (line_with_cursor.cursor.has_value() && !cursor_position_.has_value()) {
    cursor_position_ = LineColumn(line, output_column);
  }

  if (output_column < ColumnNumber(0) + width) {
    screen->WriteString(L"\n");
  }
}

void Terminal::AdjustPosition(Screen* screen) {
  CHECK(cursor_position_.has_value());
  VLOG(5) << "Setting cursor position: " << cursor_position_.value();
  screen->Move(cursor_position_.value().line, cursor_position_.value().column);
}

}  // namespace editor
}  // namespace afc
