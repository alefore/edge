#include "src/terminal.h"

#include <glog/logging.h>

#include <algorithm>
#include <cctype>
#include <iostream>

#include "src/buffer_output_producer.h"
#include "src/buffer_variables.h"
#include "src/frame_output_producer.h"
#include "src/infrastructure/dirname.h"
#include "src/infrastructure/tracker.h"
#include "src/language/lazy_string/char_buffer.h"
#include "src/line_marks.h"
#include "src/parse_tree.h"
#include "src/status_output_producer.h"

namespace gc = afc::language::gc;

using afc::infrastructure::Path;
using afc::infrastructure::PathComponent;
using afc::infrastructure::Tracker;
using afc::infrastructure::screen::LineModifier;
using afc::infrastructure::screen::LineModifierSet;
using afc::infrastructure::screen::Screen;
using afc::language::IgnoreErrors;
using afc::language::NonNull;
using afc::language::overload;
using afc::language::lazy_string::ColumnNumber;
using afc::language::lazy_string::ColumnNumberDelta;
using afc::language::lazy_string::LazyString;
using afc::language::text::LineColumn;
using afc::language::text::LineColumnDelta;
using afc::language::text::LineNumber;

namespace afc {
namespace editor {

Terminal::Terminal() : lines_cache_(1024) {}

namespace {
LineWithCursor::Generator::Vector GetLines(
    const BuffersList& buffers_list, const Status& editor_status,
    const Modifiers& modifiers,
    std::optional<gc::Root<OpenBuffer>> current_buffer, const Screen& screen) {
  LineColumnDelta screen_size = screen.size();
  LineWithCursor::Generator::Vector status_lines =
      (editor_status.GetType() == Status::Type::kPrompt ||
       editor_status.context().has_value())
          ? StatusOutput({.status = editor_status,
                          .buffer = nullptr,
                          .modifiers = modifiers,
                          .size = screen_size})
          : LineWithCursor::Generator::Vector();

  LineWithCursor::Generator::Vector output =
      buffers_list.GetLines(Widget::OutputProducerOptions{
          .size = LineColumnDelta(screen_size.line, screen_size.column),
          .main_cursor_display =
              (editor_status.GetType() == Status::Type::kPrompt ||
               (current_buffer.has_value() &&
                current_buffer->ptr()->status().GetType() ==
                    Status::Type::kPrompt))
                  ? Widget::OutputProducerOptions::MainCursorDisplay::kInactive
                  : Widget::OutputProducerOptions::MainCursorDisplay::kActive});
  CHECK_EQ(output.size(), screen_size.line);

  (editor_status.GetType() == Status::Type::kPrompt ? output : status_lines)
      .RemoveCursor();

  if (!status_lines.lines.empty()) {
    // TODO(2023-02-24): It would be more efficient to somehow convey to the
    // widget that it can skip producing status_lines.size() lines. This has to
    // be conveyed separately from the OutputProducerOptions::size::line so that
    // we avoid having things wiggle around when the status appears/disappears.
    // In other words, there's two separate concepts: how large is the view
    // size, and how many lines actually need to be rendered. The value of
    // `status_lines` should affect the 2nd but not the first.
    output.resize(screen_size.line - status_lines.size());
  }
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
  LineWithCursor::Generator::Vector lines =
      GetLines(editor_state.buffer_tree(), editor_state.status(),
               editor_state.modifiers(), editor_state.current_buffer(), screen);
  CHECK_EQ(lines.size(), screen_size.line);
  for (LineNumber line; line.ToDelta() < screen_size.line; ++line)
    WriteLine(screen, line, lines.lines[line.read()]);

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
  std::visit(overload{IgnoreErrors{},
                      [&](Path first_path) {
                        std::visit(overload{IgnoreErrors{},
                                            [&](PathComponent basename) {
                                              output = basename.ToString();
                                            }},
                                   first_path.Basename());
                      }},
             Path::FromString(output));

  if (output.size() > kMaxLength) {
    output = output.substr(0, kMaxLength - kDefaultName.size()) + kDefaultName;
  }
  return output;
}

void FlushModifiers(Screen& screen, const LineModifierSet& modifiers) {
  screen.SetModifier(LineModifier::kReset);
  for (const auto& m : modifiers) {
    screen.SetModifier(m);
  }
}

void Terminal::WriteLine(Screen& screen, LineNumber line,
                         LineWithCursor::Generator generator) {
  static Tracker tracker(L"Terminal::WriteLine");
  auto call = tracker.Call();

  if (hashes_current_lines_.size() <= line.read()) {
    CHECK_LT(line.ToDelta(), screen.size().line);
    hashes_current_lines_.resize((screen.size().line * 2 + 50).read());
  }

  auto factory = [&] {
    return GetLineDrawer(generator.generate(), screen.size().column);
  };

  LineDrawer no_hash_drawer;
  LineDrawer* drawer;
  if (generator.inputs_hash.has_value()) {
    if (hashes_current_lines_[line.read()] == generator.inputs_hash.value()) {
      return;
    }
    drawer = lines_cache_.Get(generator.inputs_hash.value(), factory).get();
  } else {
    no_hash_drawer = factory();
    drawer = &no_hash_drawer;
  }

  VLOG(8) << "Generating line for screen: " << line;
  screen.Move(LineColumn(line));
  drawer->draw_callback(screen);
  hashes_current_lines_[line.read()] = generator.inputs_hash;
  if (drawer->cursor.has_value()) {
    cursor_position_ = LineColumn(line, drawer->cursor.value());
  }
}

Terminal::LineDrawer Terminal::GetLineDrawer(LineWithCursor line_with_cursor,
                                             ColumnNumberDelta width) {
  static Tracker top_tracker(L"Terminal::GetLineDrawer");
  auto top_call = top_tracker.Call();

  Terminal::LineDrawer output;
  std::vector<decltype(LineDrawer::draw_callback)> functions;

  VLOG(6) << "Writing line of length: "
          << line_with_cursor.line.EndColumn().ToDelta();
  ColumnNumber input_column;
  ColumnNumber output_column;

  functions.push_back(
      [](Screen& screen) { screen.SetModifier(LineModifier::kReset); });

  std::map<ColumnNumber, LineModifierSet> modifiers =
      line_with_cursor.line.modifiers();
  auto modifiers_it = modifiers.lower_bound(input_column);

  while (input_column < line_with_cursor.line.EndColumn() &&
         output_column < ColumnNumber(0) + width) {
    if (line_with_cursor.cursor.has_value() &&
        input_column == line_with_cursor.cursor.value()) {
      output.cursor = output_column;
    }

    // Each iteration will advance input_column and then print between start
    // and input_column.
    auto start = input_column;
    while (input_column < line_with_cursor.line.EndColumn() &&
           output_column < ColumnNumber(0) + width &&
           (!line_with_cursor.cursor.has_value() ||
            input_column != line_with_cursor.cursor.value() ||
            output.cursor == output_column) &&
           (modifiers_it == modifiers.end() ||
            modifiers_it->first > input_column)) {
      output_column += ColumnNumberDelta(
          wcwidth(line_with_cursor.line.contents().get(input_column)));
      ++input_column;
    }

    if (start != input_column) {
      static Tracker tracker(L"Terminal::GetLineDrawer: Call WriteString");
      auto call = tracker.Call();
      LazyString str = line_with_cursor.line.contents().Substring(
          start, input_column - start);
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
        [](Screen& screen) { screen.WriteString(LazyString{L"\n"}); });
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
