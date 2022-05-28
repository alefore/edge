#include "src/terminal.h"

#include <glog/logging.h>

#include <algorithm>
#include <cctype>
#include <iostream>

#include "src/buffer_output_producer.h"
#include "src/buffer_variables.h"
#include "src/char_buffer.h"
#include "src/frame_output_producer.h"
#include "src/infrastructure/dirname.h"
#include "src/infrastructure/tracker.h"
#include "src/line_marks.h"
#include "src/parse_tree.h"
#include "src/status_output_producer.h"

namespace afc {
namespace editor {
using infrastructure::Path;
using infrastructure::Tracker;

namespace gc = language::gc;

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

Terminal::Terminal() : lines_cache_(1024) {}

namespace {
LineWithCursor::Generator::Vector GetLines(const EditorState& editor_state,
                                           const Screen& screen) {
  LineColumnDelta screen_size = screen.size();
  LineWithCursor::Generator::Vector status_lines =
      StatusOutput({.status = editor_state.status(),
                    .buffer = nullptr,
                    .modifiers = editor_state.modifiers(),
                    .size = screen_size});

  std::optional<gc::Root<OpenBuffer>> buffer = editor_state.current_buffer();
  LineWithCursor::Generator::Vector output =
      editor_state.buffer_tree().GetLines(
          {.size = LineColumnDelta(screen_size.line - status_lines.size(),
                                   screen_size.column),
           .main_cursor_behavior =
               (editor_state.status().GetType() == Status::Type::kPrompt ||
                (buffer.has_value() &&
                 buffer->ptr()->status().GetType() == Status::Type::kPrompt))
                   ? Widget::OutputProducerOptions::MainCursorBehavior::
                         kHighlight
                   : Widget::OutputProducerOptions::MainCursorBehavior::
                         kIgnore});
  CHECK_EQ(output.size(), screen_size.line - status_lines.size());

  (editor_state.status().GetType() == Status::Type::kPrompt ? output
                                                            : status_lines)
      .RemoveCursor();
  output.Append(std::move(status_lines));
  return output;
}
}  // namespace

void Terminal::Display(const EditorState& editor_state, Screen& screen,
                       const EditorState::ScreenState& screen_state) {
  static Tracker tracker(L"Terminal::Display");
  auto call = tracker.Call();

  if (screen_state.needs_hard_redraw) {
    screen.HardRefresh();
    hashes_current_lines_.clear();
    lines_cache_.Clear();
  }
  screen.Move(LineColumn());

  LineColumnDelta screen_size = screen.size();
  std::optional<gc::Root<OpenBuffer>> buffer = editor_state.current_buffer();
  LineWithCursor::Generator::Vector lines = GetLines(editor_state, screen);
  CHECK_EQ(lines.size(), screen_size.line);
  for (LineNumber line; line.ToDelta() < screen_size.line; ++line)
    WriteLine(screen, line, lines.lines[line.line]);

  if (editor_state.status().GetType() == Status::Type::kPrompt ||
      (buffer.has_value() &&
       buffer->ptr()->status().GetType() == Status::Type::kPrompt) ||
      (buffer.has_value() &&
       !buffer->ptr()->Read(buffer_variables::atomic_lines) &&
       cursor_position_.has_value())) {
    screen.SetCursorVisibility(Screen::NORMAL);
    AdjustPosition(screen);
  } else {
    screen.SetCursorVisibility(Screen::INVISIBLE);
  }
  screen.Refresh();
  screen.Flush();
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
std::wstring TransformCommandNameForStatus(std::wstring name) {
  static const std::wstring kDefaultName = L"…";
  static const size_t kMaxLength = 5;

  size_t index = 0;
  if (name.size() > 2 && name[0] == L'$' && name[1] == L' ') {
    index = 2;
  }

  index = name.find_first_not_of(L' ', index);  // Skip spaces.
  if (index == std::wstring::npos) {
    return kDefaultName;
  }
  size_t end = name.find_first_of(L' ', index);
  std::wstring output = name.substr(
      index, end == std::wstring::npos ? std::wstring::npos : end - index);
  if (auto first_path = Path::FromString(output); !IsError(first_path)) {
    if (auto basename = first_path.value().Basename(); !IsError(basename)) {
      output = basename.value().ToString();
    }
  }

  if (output.size() > kMaxLength) {
    output = output.substr(0, kMaxLength - kDefaultName.size()) + kDefaultName;
  }
  return output;
}

void FlushModifiers(Screen& screen, const LineModifierSet& modifiers) {
  screen.SetModifier(LineModifier::RESET);
  for (const auto& m : modifiers) {
    screen.SetModifier(m);
  }
}

void Terminal::WriteLine(Screen& screen, LineNumber line,
                         LineWithCursor::Generator generator) {
  static Tracker tracker(L"Terminal::WriteLine");
  auto call = tracker.Call();

  if (hashes_current_lines_.size() <= line.line) {
    CHECK_LT(line.ToDelta(), screen.size().line);
    hashes_current_lines_.resize(screen.size().line.line_delta * 2 + 50);
  }

  auto factory = [&] {
    return GetLineDrawer(generator.generate(), screen.size().column);
  };

  LineDrawer no_hash_drawer;
  LineDrawer* drawer;
  if (generator.inputs_hash.has_value()) {
    if (hashes_current_lines_[line.line] == generator.inputs_hash.value()) {
      return;
    }
    drawer = lines_cache_.Get(generator.inputs_hash.value(), factory);
  } else {
    no_hash_drawer = factory();
    drawer = &no_hash_drawer;
  }

  VLOG(8) << "Generating line for screen: " << line;
  screen.Move(LineColumn(line));
  drawer->draw_callback(screen);
  hashes_current_lines_[line.line] = generator.inputs_hash;
  if (drawer->cursor.has_value()) {
    cursor_position_ = LineColumn(line, drawer->cursor.value());
  }
}

Terminal::LineDrawer Terminal::GetLineDrawer(LineWithCursor line_with_cursor,
                                             ColumnNumberDelta width) {
  static Tracker tracker(L"Terminal::GetLineDrawer");
  auto call = tracker.Call();

  Terminal::LineDrawer output;
  std::vector<decltype(LineDrawer::draw_callback)> functions;

  VLOG(6) << "Writing line of length: "
          << line_with_cursor.line->EndColumn().ToDelta();
  ColumnNumber input_column;
  ColumnNumber output_column;

  functions.push_back(
      [](Screen& screen) { screen.SetModifier(LineModifier::RESET); });

  std::map<ColumnNumber, LineModifierSet> modifiers =
      line_with_cursor.line->modifiers();
  auto modifiers_it = modifiers.lower_bound(input_column);

  while (input_column < line_with_cursor.line->EndColumn() &&
         output_column < ColumnNumber(0) + width) {
    if (line_with_cursor.cursor.has_value() &&
        input_column == line_with_cursor.cursor.value()) {
      output.cursor = output_column;
    }

    // Each iteration will advance input_column and then print between start
    // and input_column.
    auto start = input_column;
    while (input_column < line_with_cursor.line->EndColumn() &&
           output_column < ColumnNumber(0) + width &&
           (!line_with_cursor.cursor.has_value() ||
            input_column != line_with_cursor.cursor.value() ||
            output.cursor == output_column) &&
           (modifiers_it == modifiers.end() ||
            modifiers_it->first > input_column)) {
      output_column += ColumnNumberDelta(
          wcwidth(line_with_cursor.line->contents()->get(input_column)));
      ++input_column;
    }

    if (start != input_column) {
      static Tracker tracker(L"Terminal::GetLineDrawer: Call WriteString");
      auto call = tracker.Call();
      auto str = Substring(line_with_cursor.line->contents(), start,
                           input_column - start);
      functions.push_back([str](Screen& screen) { screen.WriteString(str); });
    }

    if (modifiers_it != modifiers.end()) {
      CHECK_GE(modifiers_it->first, input_column);
      if (modifiers_it->first == input_column) {
        LineModifierSet modifiers_set = modifiers_it->second;
        functions.push_back([modifiers_set](Screen& screen) {
          FlushModifiers(screen, modifiers_set);
        });
        ++modifiers_it;
      }
    }
  }

  if (line_with_cursor.cursor.has_value() && !output.cursor.has_value()) {
    output.cursor = output_column;
  }

  if (output_column < ColumnNumber(0) + width) {
    functions.push_back(
        [](Screen& screen) { screen.WriteString(NewLazyString(L"\n")); });
  }
  output.draw_callback = [functions = std::move(functions)](Screen& screen) {
    for (auto& f : functions) {
      f(screen);
    }
  };
  return output;
}

void Terminal::AdjustPosition(Screen& screen) {
  CHECK(cursor_position_.has_value());
  VLOG(5) << "Setting cursor position: " << cursor_position_.value();
  screen.Move(LineColumn(cursor_position_.value()));
}

}  // namespace editor
}  // namespace afc
