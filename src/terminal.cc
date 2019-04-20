#include "src/terminal.h"

#include <glog/logging.h>

#include <algorithm>
#include <cctype>
#include <iostream>

#include "src/buffer_variables.h"
#include "src/delegating_output_receiver.h"
#include "src/delegating_output_receiver_with_internal_modifiers.h"
#include "src/dirname.h"
#include "src/line_marks.h"
#include "src/output_receiver.h"
#include "src/output_receiver_optimizer.h"
#include "src/parse_tree.h"
#include "src/screen_output_receiver.h"

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

namespace {
// Returns the number of initial columns to skip, corresponding to output that
// prefixes the actual line contents.
size_t GetInitialPrefixSize(const OpenBuffer& buffer) {
  return buffer.Read(buffer_variables::paste_mode())
             ? 0
             : 1 + std::to_wstring(buffer.lines_size()).size();
}

size_t GetCurrentColumn(OpenBuffer* buffer) {
  if (buffer->lines_size() == 0) {
    return 0;
  } else if (buffer->position().line >= buffer->lines_size()) {
    return buffer->contents()->back()->size();
  } else if (!buffer->IsLineFiltered(buffer->position().line)) {
    return 0;
  } else {
    return min(buffer->position().column,
               buffer->LineAt(buffer->position().line)->size());
  }
}

size_t GetDesiredViewStartColumn(Screen* screen, OpenBuffer* buffer) {
  if (buffer->Read(buffer_variables::wrap_long_lines())) {
    return 0;
  }
  size_t effective_size = screen->columns() - 1;
  effective_size -= min(effective_size, GetInitialPrefixSize(*buffer));
  size_t column = GetCurrentColumn(buffer);
  return column - min(column, effective_size);
}

std::wstring GetInitialPrefix(const OpenBuffer& buffer, int line) {
  if (buffer.Read(buffer_variables::paste_mode())) {
    return L"";
  }
  std::wstring number = std::to_wstring(line + 1);
  std::wstring padding(GetInitialPrefixSize(buffer) - number.size() - 1, L' ');
  return padding + number + L':';
}

wchar_t ComputeScrollBarCharacter(size_t line, size_t lines_size,
                                  size_t view_start, size_t lines_to_show) {
  // Each line is split into two units (upper and bottom halves). All units in
  // this function are halves (of a line).
  DCHECK_GE(line, view_start);
  DCHECK_LT(line - view_start, lines_to_show)
      << "Line is " << line << " and view_start is " << view_start
      << ", which exceeds lines_to_show of " << lines_to_show;
  DCHECK_LT(view_start, lines_size);
  size_t halves_to_show = lines_to_show * 2;

  // Number of halves the bar should take.
  size_t bar_size =
      max(size_t(1),
          size_t(std::round(halves_to_show *
                            static_cast<double>(lines_to_show) / lines_size)));

  // Bar will be shown in lines in interval [bar, end] (units are halves).
  size_t start =
      std::round(halves_to_show * static_cast<double>(view_start) / lines_size);
  size_t end = start + bar_size;

  size_t current = 2 * (line - view_start);
  if (current < start - (start % 2) || current >= end) {
    return L' ';
  } else if (start == current + 1) {
    return L'▄';
  } else if (current + 1 == end) {
    return L'▀';
  } else {
    return L'█';
  }
}

void Draw(size_t pos, wchar_t padding_char, wchar_t final_char,
          wchar_t connect_final_char, wstring* output) {
  CHECK_LT(pos, output->size());
  for (size_t i = 0; i < pos; i++) {
    output->at(i) = padding_char;
  }
  output->at(pos) = (pos + 1 == output->size() || output->at(pos + 1) == L' ' ||
                     output->at(pos + 1) == L'│')
                        ? final_char
                        : connect_final_char;
}

wstring DrawTree(size_t line, size_t lines_size, const ParseTree& root) {
  // Route along the tree where each child ends after previous line.
  vector<const ParseTree*> route_begin;
  if (line > 0) {
    route_begin = MapRoute(
        root,
        FindRouteToPosition(
            root, LineColumn(line - 1, std::numeric_limits<size_t>::max())));
    CHECK(!route_begin.empty() && *route_begin.begin() == &root);
    route_begin.erase(route_begin.begin());
  }

  // Route along the tree where each child ends after current line.
  vector<const ParseTree*> route_end;
  if (line < lines_size - 1) {
    route_end = MapRoute(
        root, FindRouteToPosition(
                  root, LineColumn(line, std::numeric_limits<size_t>::max())));
    CHECK(!route_end.empty() && *route_end.begin() == &root);
    route_end.erase(route_end.begin());
  }

  wstring output(root.depth, L' ');
  size_t index_begin = 0;
  size_t index_end = 0;
  while (index_begin < route_begin.size() || index_end < route_end.size()) {
    if (index_begin == route_begin.size()) {
      Draw(route_end[index_end]->depth, L'─', L'╮', L'┬', &output);
      index_end++;
      continue;
    }
    if (index_end == route_end.size()) {
      Draw(route_begin[index_begin]->depth, L'─', L'╯', L'┴', &output);
      index_begin++;
      continue;
    }

    if (route_begin[index_begin]->depth > route_end[index_end]->depth) {
      Draw(route_begin[index_begin]->depth, L'─', L'╯', L'┴', &output);
      index_begin++;
      continue;
    }

    if (route_end[index_end]->depth > route_begin[index_begin]->depth) {
      Draw(route_end[index_end]->depth, L'─', L'╮', L'┬', &output);
      index_end++;
      continue;
    }

    if (route_begin[index_begin] == route_end[index_end]) {
      output[route_begin[index_begin]->depth] = L'│';
      index_begin++;
      index_end++;
      continue;
    }

    Draw(route_end[index_end]->depth, L'─', L'┤', L'┼', &output);
    index_begin++;
    index_end++;
  }
  return output;
}
}  // namespace

void Terminal::Display(EditorState* editor_state, Screen* screen,
                       const EditorState::ScreenState& screen_state) {
  if (screen_state.needs_hard_redraw) {
    screen->HardRefresh();
    editor_state->ScheduleRedraw();
  }
  if (!editor_state->has_current_buffer()) {
    if (screen_state.needs_redraw) {
      screen->Clear();
    }
    ShowStatus(*editor_state, screen);
    screen->Refresh();
    screen->Flush();
    return;
  }
  int screen_lines = screen->lines();
  auto& buffer = editor_state->current_buffer()->second;
  if (buffer->Read(buffer_variables::reload_on_display())) {
    buffer->Reload();
  }
  size_t line = min(buffer->position().line, buffer->contents()->size() - 1);
  size_t margin_lines =
      min(screen_lines / 2,
          max(static_cast<int>(
                  ceil(buffer->Read(buffer_variables::margin_lines_ratio()) *
                       screen_lines)),
              max(buffer->Read(buffer_variables::margin_lines()), 0)));
  margin_lines = min(margin_lines, static_cast<size_t>(screen_lines) / 2 - 1);
  size_t view_start = static_cast<size_t>(
      max(0, buffer->Read(buffer_variables::view_start_line())));
  if (view_start > line - min(margin_lines, line) &&
      (buffer->child_pid() != -1 || buffer->fd() == -1)) {
    buffer->Set(buffer_variables::view_start_line(),
                line - min(margin_lines, line));
    editor_state->ScheduleRedraw();
  } else if (view_start + screen_lines - 1 <=
             min(buffer->lines_size() - 1, line + margin_lines)) {
    buffer->Set(
        buffer_variables::view_start_line(),
        min(buffer->lines_size() - 1, line + margin_lines) - screen_lines + 2);
    editor_state->ScheduleRedraw();
  }

  auto view_start_column = GetDesiredViewStartColumn(screen, buffer.get());
  if (static_cast<size_t>(
          max(0, buffer->Read(buffer_variables::view_start_column()))) !=
      view_start_column) {
    buffer->Set(buffer_variables::view_start_column(), view_start_column);
    editor_state->ScheduleRedraw();
  }

  if (buffer->Read(buffer_variables::atomic_lines()) &&
      buffer->last_highlighted_line() != buffer->position().line) {
    editor_state->ScheduleRedraw();
  }

  if (screen_state.needs_redraw) {
    ShowBuffer(editor_state, screen);
  }
  ShowStatus(*editor_state, screen);
  if (editor_state->status_prompt()) {
    screen->SetCursorVisibility(Screen::NORMAL);
  } else if (buffer->Read(buffer_variables::atomic_lines()) ||
             !cursor_position_.has_value()) {
    screen->SetCursorVisibility(Screen::INVISIBLE);
  } else {
    screen->SetCursorVisibility(Screen::NORMAL);
    AdjustPosition(screen);
  }
  screen->Refresh();
  screen->Flush();
  editor_state->set_visible_lines(static_cast<size_t>(screen_lines - 1));
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
  wstring status;
  if (editor_state.has_current_buffer() && !editor_state.is_status_warning()) {
    const auto modifiers = editor_state.modifiers();
    auto buffer = editor_state.current_buffer()->second;
    status.push_back('[');
    if (buffer->current_position_line() >= buffer->contents()->size()) {
      status += L"<EOF>";
    } else {
      status += to_wstring(buffer->current_position_line() + 1);
    }
    status += L" of " + to_wstring(buffer->contents()->size()) + L", " +
              to_wstring(buffer->current_position_col() + 1);

    status += L"] ";

    for (auto& it : *editor_state.buffers()) {
      if (it.second->ShouldDisplayProgress()) {
        auto name = TransformCommandNameForStatus(
            it.second->Read(buffer_variables::name()));
        size_t progress = it.second->Read(buffer_variables::progress()) %
                          (4 + 2 * name.size());
        if (progress == 0 || progress == 1) {
          static const std::vector<wstring> begin = {L"◟", L"◜"};
          status += begin[progress] + name + L" ";
        } else if (progress < 2 + name.size()) {
          int split = progress - 2;
          status += L" " + name.substr(0, split + 1) + L"̅" +
                    name.substr(split + 1) + L" ";
        } else if (progress < 2 + name.size() + 2) {
          static const std::vector<wstring> end = {L"◝", L"◞"};
          status += L" " + name + end[progress - 2 - name.size()];
        } else {
          int split = name.size() - (progress - 2 - name.size() - 2) - 1;
          status += L" " + name.substr(0, split + 1) + L"̲" +
                    name.substr(split + 1) + L" ";
        }
      }
    }

    auto marks_text = buffer->GetLineMarksText();
    if (!marks_text.empty()) {
      status += marks_text + L" ";
    }

    auto active_cursors = buffer->active_cursors()->size();
    if (active_cursors != 1) {
      status += L" " +
                (buffer->Read(buffer_variables::multiple_cursors())
                     ? wstring(L"CURSORS")
                     : wstring(L"cursors")) +
                L":" + to_wstring(active_cursors) + L" ";
    }

    wstring flags = buffer->FlagsString();
    if (editor_state.repetitions() != 1) {
      flags += to_wstring(editor_state.repetitions());
    }
    if (modifiers.default_direction == BACKWARDS) {
      flags += L" REVERSE";
    } else if (modifiers.direction == BACKWARDS) {
      flags += L" reverse";
    }

    if (modifiers.default_insertion == Modifiers::REPLACE) {
      flags += L" REPLACE";
    } else if (modifiers.insertion == Modifiers::REPLACE) {
      flags += L" replace";
    }

    if (modifiers.strength == Modifiers::Strength::kStrong) {
      flags += L" 💪";
    }

    wstring structure;
    if (editor_state.structure() == StructureTree()) {
      structure = L"tree<" + to_wstring(buffer->tree_depth()) + L">";
    } else if (editor_state.structure() != StructureChar()) {
      structure = editor_state.structure()->ToString();
    }
    if (!structure.empty()) {
      if (editor_state.sticky_structure()) {
        transform(structure.begin(), structure.end(), structure.begin(),
                  ::toupper);
      }
      switch (editor_state.structure_range()) {
        case Modifiers::ENTIRE_STRUCTURE:
          break;
        case Modifiers::FROM_BEGINNING_TO_CURRENT_POSITION:
          structure = L"[..." + structure;
          break;
        case Modifiers::FROM_CURRENT_POSITION_TO_END:
          structure = structure + L"...]";
          break;
      }
      flags += L"(" + structure + L")";
    }

    if (!flags.empty()) {
      status += L" " + flags + L" ";
    }

    if (editor_state.status().empty()) {
      status += L"“" + GetBufferContext(editor_state, buffer) + L"” ";
    }
  }

  if (!editor_state.is_status_warning()) {
    int running = 0;
    int failed = 0;
    for (const auto& it : *editor_state.buffers()) {
      CHECK(it.second != nullptr);
      if (it.second->child_pid() != -1) {
        running++;
      } else {
        int status = it.second->child_exit_status();
        if (WIFEXITED(status)) {
          if (WEXITSTATUS(status)) {
            failed++;
          }
        }
      }
    }
    if (running > 0) {
      status += L"  🏃" + to_wstring(running) + L"  ";
    }
    if (failed > 0) {
      status += L"  💥" + to_wstring(failed) + L"  ";
    }
  }

  size_t status_column = 0;
  for (size_t i = 0; i < status.size(); i++) {
    status_column += wcwidth(status[i]);
  }
  status += editor_state.status();
  if (status.size() < screen->columns()) {
    status += wstring(screen->columns() - status.size(), ' ');
  } else if (status.size() > screen->columns()) {
    status = status.substr(0, screen->columns());
  }
  screen->Move(screen->lines() - 1, 0);
  if (editor_state.is_status_warning()) {
    screen->SetModifier(LineModifier::RED);
    screen->SetModifier(LineModifier::BOLD);
  }
  screen->WriteString(status.c_str());
  if (editor_state.is_status_warning()) {
    screen->SetModifier(LineModifier::RESET);
  }
  if (editor_state.status_prompt()) {
    status_column += editor_state.status_prompt_column();
    screen->Move(screen->lines() - 1, min(status_column, screen->columns()));
  }
}

wstring Terminal::GetBufferContext(const EditorState& editor_state,
                                   const shared_ptr<OpenBuffer>& buffer) {
  auto marks = buffer->GetLineMarks();
  auto current_line_marks = marks->find(buffer->position().line);
  if (current_line_marks != marks->end()) {
    auto mark = current_line_marks->second;
    auto source = editor_state.buffers()->find(mark.source);
    if (source != editor_state.buffers()->end() &&
        source->second->contents()->size() > mark.source_line) {
      return source->second->contents()->at(mark.source_line)->ToString();
    }
  }
  return buffer->Read(buffer_variables::name());
}

class WithPrefixOutputReceiver : public DelegatingOutputReceiver {
 public:
  WithPrefixOutputReceiver(std::unique_ptr<OutputReceiver> delegate,
                           wstring prefix, LineModifier prefix_modifier)
      : DelegatingOutputReceiver(std::move(delegate)), prefix_length_([=]() {
          AddModifier(prefix_modifier);
          AddString(prefix);
          AddModifier(LineModifier::RESET);
          return DelegatingOutputReceiver::column();
        }()) {}

  void SetTabsStart(size_t columns) override {
    DelegatingOutputReceiver::SetTabsStart(prefix_length_ + columns);
  }

  size_t column() override {
    if (DelegatingOutputReceiver::column() < prefix_length_) {
      return 0;
    }
    return DelegatingOutputReceiver::column() - prefix_length_;
  }
  size_t width() override {
    if (DelegatingOutputReceiver::width() < prefix_length_) {
      return 0;
    }
    return DelegatingOutputReceiver::width() - prefix_length_;
  }

 private:
  const size_t prefix_length_;
};

class HighlightedLineOutputReceiver : public DelegatingOutputReceiver {
 public:
  HighlightedLineOutputReceiver(std::unique_ptr<OutputReceiver> delegate)
      : DelegatingOutputReceiver(std::move(delegate)) {
    DelegatingOutputReceiver::AddModifier(LineModifier::REVERSE);
  }

  void AddModifier(LineModifier modifier) {
    switch (modifier) {
      case LineModifier::RESET:
        DelegatingOutputReceiver::AddModifier(LineModifier::RESET);
        DelegatingOutputReceiver::AddModifier(LineModifier::REVERSE);
        break;
      default:
        DelegatingOutputReceiver::AddModifier(modifier);
    }
  }
};

class CursorsHighlighter
    : public DelegatingOutputReceiverWithInternalModifiers {
 public:
  struct Options {
    std::unique_ptr<OutputReceiver> delegate;

    // A set with all the columns in the current line in which there are
    // cursors that should be drawn.
    set<size_t> columns;

    bool multiple_cursors;

    std::optional<size_t> active_cursor_input;

    // Output parameter. If the active cursor is found in this line, we set this
    // to the column in the screen to which it should be moved. This is used to
    // handle multi-width characters.
    std::optional<size_t>* active_cursor_output;
  };

  explicit CursorsHighlighter(Options options)
      : DelegatingOutputReceiverWithInternalModifiers(
            std::move(options.delegate),
            DelegatingOutputReceiverWithInternalModifiers::Preference::
                kInternal),
        options_(std::move(options)),
        next_cursor_(options_.columns.begin()) {
    CheckInvariants();
  }

  void AddCharacter(wchar_t c) {
    CheckInvariants();
    bool at_cursor =
        next_cursor_ != options_.columns.end() && *next_cursor_ == column_read_;
    if (at_cursor) {
      ++next_cursor_;
      CHECK(next_cursor_ == options_.columns.end() ||
            *next_cursor_ > column_read_);
      bool is_active = options_.active_cursor_input.has_value() &&
                       options_.active_cursor_input.value() == column_read_;
      AddInternalModifier(LineModifier::REVERSE);
      AddInternalModifier(is_active || options_.multiple_cursors
                              ? LineModifier::CYAN
                              : LineModifier::BLUE);
      if (is_active && options_.active_cursor_output != nullptr) {
        *options_.active_cursor_output =
            DelegatingOutputReceiverWithInternalModifiers::column();
      }
    }

    DelegatingOutputReceiver::AddCharacter(c);
    if (at_cursor) {
      AddInternalModifier(LineModifier::RESET);
    }
    column_read_++;
    CheckInvariants();
  }

  void AddString(const wstring& str) {
    size_t str_pos = 0;
    while (str_pos < str.size()) {
      CheckInvariants();

      // Compute the position of the next cursor relative to the start of this
      // string.
      size_t next_column = (next_cursor_ == options_.columns.end())
                               ? str.size()
                               : *next_cursor_ + str_pos - column_read_;
      if (next_column > str_pos) {
        size_t len = next_column - str_pos;
        DelegatingOutputReceiver::AddString(str.substr(str_pos, len));
        column_read_ += len;
        str_pos += len;
      }

      CheckInvariants();

      if (str_pos < str.size()) {
        CHECK(next_cursor_ != options_.columns.end());
        CHECK_EQ(*next_cursor_, column_read_);
        AddCharacter(str[str_pos]);
        str_pos++;
      }
      CheckInvariants();
    }
  }

 private:
  void CheckInvariants() {
    if (next_cursor_ != options_.columns.end()) {
      CHECK_GE(*next_cursor_, column_read_);
    }
  }

  const Options options_;

  // Points to the first element in columns_ that is greater than or equal to
  // the current position.
  set<size_t>::const_iterator next_cursor_;
  size_t column_read_ = 0;
};

class ParseTreeHighlighter : public DelegatingOutputReceiver {
 public:
  explicit ParseTreeHighlighter(std::unique_ptr<OutputReceiver> delegate,
                                size_t begin, size_t end)
      : DelegatingOutputReceiver(std::move(delegate)),
        begin_(begin),
        end_(end) {}

  void AddCharacter(wchar_t c) override {
    size_t position = column();
    // TODO: Optimize: Don't add it for each character, just at the start.
    if (begin_ <= position && position < end_) {
      AddModifier(LineModifier::BLUE);
    }

    DelegatingOutputReceiver::AddCharacter(c);

    // TODO: Optimize: Don't add it for each character, just at the end.
    if (c != L'\n') {
      AddModifier(LineModifier::RESET);
    }
  }

  void AddString(const wstring& str) override {
    // TODO: Optimize.
    if (str == L"\n") {
      DelegatingOutputReceiver::AddString(str);
      return;
    }
    for (auto& c : str) {
      AddCharacter(c);
    }
  }

 private:
  const size_t begin_;
  const size_t end_;
};

class ParseTreeHighlighterTokens
    : public DelegatingOutputReceiverWithInternalModifiers {
 public:
  // A OutputReceiver implementation that merges modifiers from the syntax tree
  // (with modifiers from the line). When modifiers from the line are present,
  // they override modifiers from the syntax tree.
  //
  // largest_column_with_tree: Position after which modifiers from the syntax
  // tree will no longer apply. This ensures that "continuation" modifiers
  // (that were active at the last character in the line) won't continue to
  // affect the padding and/or scrollbar).
  ParseTreeHighlighterTokens(std::unique_ptr<OutputReceiver> delegate,
                             const ParseTree* root, size_t line,
                             size_t largest_column_with_tree)
      : DelegatingOutputReceiverWithInternalModifiers(
            std::move(delegate), DelegatingOutputReceiverWithInternalModifiers::
                                     Preference::kExternal),
        root_(root),
        largest_column_with_tree_(largest_column_with_tree),
        line_(line),
        current_({root}) {
    UpdateCurrent(LineColumn(line_, 0));
  }

  void AddCharacter(wchar_t c) override {
    LineColumn position(line_, column_read_++);
    if (position.column >= largest_column_with_tree_) {
      if (position.column == largest_column_with_tree_) {
        AddInternalModifier(LineModifier::RESET);
      }
      DelegatingOutputReceiver::AddCharacter(c);
      return;
    }
    if (!current_.empty() && current_.back()->range.end <= position) {
      UpdateCurrent(position);
    }

    AddInternalModifier(LineModifier::RESET);
    if (!current_.empty() && !has_high_modifiers()) {
      for (auto& t : current_) {
        if (t->range.Contains(position)) {
          for (auto& modifier : t->modifiers) {
            AddInternalModifier(modifier);
          }
        }
      }
    }
    DelegatingOutputReceiver::AddCharacter(c);
  }

  void AddString(const wstring& str) override {
    // TODO: Optimize.
    if (str == L"\n") {
      DelegatingOutputReceiver::AddString(str);
      column_read_ = 0;
      return;
    }
    for (auto& c : str) {
      DelegatingOutputReceiver::AddCharacter(c);
    }
  }

 private:
  void UpdateCurrent(LineColumn position) {
    // Go up the tree until we're at a root that includes position.
    while (!current_.empty() && current_.back()->range.end <= position) {
      current_.pop_back();
    }

    if (current_.empty()) {
      return;
    }

    // Go down the tree. At each position, pick the first children that ends
    // after position (it may also start *after* position).
    while (!current_.back()->children.empty()) {
      auto it = current_.back()->children.UpperBound(
          position, [](const LineColumn& position, const ParseTree& candidate) {
            return position < candidate.range.end;
          });
      if (it == current_.back()->children.end()) {
        return;
      }
      current_.push_back(&*it);
    }
  }

  // Keeps track of the modifiers coming from the parent, so as to not lose
  // that information when we reset our own.
  const ParseTree* root_;
  const size_t largest_column_with_tree_;
  const size_t line_;
  std::vector<const ParseTree*> current_;
  size_t column_read_ = 0;
};

LineColumn Terminal::GetNextLine(const OpenBuffer& buffer, size_t columns,
                                 LineColumn position) {
  // TODO: This is wrong: it doesn't account for multi-width characters.
  // TODO: This is wrong: it doesn't take int account line filters.
  if (position.line >= buffer.lines_size()) {
    return LineColumn(std::numeric_limits<size_t>::max());
  }
  position.column += columns;
  if (position.column >= buffer.LineAt(position.line)->size() ||
      !buffer.Read(buffer_variables::wrap_long_lines())) {
    position.line++;
    position.column = buffer.Read(buffer_variables::view_start_column());
    if (position.line >= buffer.lines_size()) {
      return LineColumn(std::numeric_limits<size_t>::max());
    }
  }
  return position;
}

// Adds spaces until we're at a given position.
void AddPadding(size_t column, const LineModifierSet& modifiers,
                OutputReceiver* output_receiver) {
  if (output_receiver->column() >= column ||
      column >= output_receiver->width()) {
    return;
  }

  size_t padding = column - output_receiver->column();
  for (auto it : modifiers) {
    output_receiver->AddModifier(it);
  }
  output_receiver->AddString(wstring(padding, L' '));
  output_receiver->AddModifier(LineModifier::RESET);
  CHECK_LE(output_receiver->column(), output_receiver->width());
}

void ShowAdditionalData(
    OpenBuffer* buffer, const Line& line, LineColumn position,
    size_t lines_to_show, const ParseTree* full_file_parse_tree,
    OutputReceiver* output_receiver,
    std::unordered_set<const OpenBuffer*>* output_buffers_shown) {
  auto target_buffer_value = line.environment()->Lookup(
      L"buffer", vm::VMTypeMapper<OpenBuffer*>::vmtype);
  const auto target_buffer =
      (target_buffer_value != nullptr &&
       target_buffer_value->user_value != nullptr)
          ? static_cast<OpenBuffer*>(target_buffer_value->user_value.get())
          : buffer;

  const auto line_width =
      static_cast<size_t>(max(0, buffer->Read(buffer_variables::line_width())));

  auto all_marks = buffer->GetLineMarks();
  auto marks = all_marks->equal_range(position.line);

  wchar_t info_char = L'•';
  wstring additional_information;

  if (target_buffer != buffer) {
    if (output_buffers_shown->insert(target_buffer).second) {
      additional_information = target_buffer->FlagsString();
    }
  } else if (marks.first != marks.second) {
    output_receiver->AddModifier(LineModifier::RED);
    info_char = '!';

    // Prefer fresh over expired marks.
    while (next(marks.first) != marks.second &&
           marks.first->second.IsExpired()) {
      ++marks.first;
    }

    const LineMarks::Mark& mark = marks.first->second;
    if (mark.source_line_content != nullptr) {
      additional_information = L"(old) " + mark.source_line_content->ToString();
    } else {
      auto source = buffer->editor()->buffers()->find(mark.source);
      if (source != buffer->editor()->buffers()->end() &&
          source->second->contents()->size() > mark.source_line) {
        output_receiver->AddModifier(LineModifier::BOLD);
        additional_information =
            source->second->contents()->at(mark.source_line)->ToString();
      } else {
        additional_information = L"(dead)";
      }
    }
  } else if (line.modified()) {
    output_receiver->AddModifier(LineModifier::GREEN);
    info_char = L'•';
  } else {
    output_receiver->AddModifier(LineModifier::DIM);
  }
  if (output_receiver->column() <= line_width &&
      output_receiver->column() < output_receiver->width()) {
    output_receiver->AddCharacter(info_char);
  }
  output_receiver->AddModifier(LineModifier::RESET);

  const auto view_start_line =
      buffer->Read(buffer_variables::view_start_line());

  if (additional_information.empty()) {
    auto parse_tree = buffer->simplified_parse_tree();
    if (parse_tree != nullptr) {
      additional_information +=
          DrawTree(position.line, buffer->lines_size(), *parse_tree);
    }
    if (buffer->Read(buffer_variables::scrollbar()) &&
        buffer->lines_size() > lines_to_show) {
      additional_information += ComputeScrollBarCharacter(
          position.line, buffer->lines_size(), view_start_line, lines_to_show);
    }
    if (full_file_parse_tree != nullptr &&
        !full_file_parse_tree->children.empty()) {
      additional_information += DrawTree(position.line - view_start_line,
                                         lines_to_show, *full_file_parse_tree);
    }
  }

  if (output_receiver->column() > line_width + 1) {
    VLOG(6) << "Trimming the beginning of additional_information.";
    additional_information = additional_information.substr(
        min(additional_information.size(),
            output_receiver->column() - (line_width + 1)));
  }
  if (output_receiver->width() >= output_receiver->column()) {
    VLOG(6) << "Trimming the end of additional_information.";
    additional_information = additional_information.substr(
        0, min(additional_information.size(),
               output_receiver->width() - output_receiver->column()));
  }
  output_receiver->AddString(additional_information);
}

void Terminal::ShowBuffer(const EditorState* editor_state, Screen* screen) {
  const shared_ptr<OpenBuffer> buffer = editor_state->current_buffer()->second;
  size_t lines_to_show = static_cast<size_t>(screen->lines()) - 1;
  screen->Move(0, 0);

  buffer->set_last_highlighted_line(-1);

  // Key is line number.
  std::map<size_t, std::set<size_t>> cursors;
  for (auto cursor : *buffer->active_cursors()) {
    cursors[cursor.line].insert(cursor.column);
  }

  auto root = buffer->parse_tree();
  auto current_tree = buffer->current_tree(root.get());

  Line::OutputOptions line_output_options;
  buffer->set_lines_for_zoomed_out_tree(lines_to_show);
  auto zoomed_out_tree = buffer->zoomed_out_tree();

  std::unordered_set<const OpenBuffer*> buffers_shown;

  cursor_position_ = std::nullopt;

  size_t last_line = std::numeric_limits<size_t>::max();

  LineColumn position(
      static_cast<size_t>(
          max(0, buffer->Read(buffer_variables::view_start_line()))),
      buffer->Read(buffer_variables::view_start_column()));

  for (size_t i = 0; i < lines_to_show; i++) {
    std::unique_ptr<OutputReceiver> line_output_receiver =
        std::make_unique<OutputReceiverOptimizer>(
            NewScreenOutputReceiver(screen));

    if (position.line >= buffer->lines_size()) {
      line_output_receiver->AddString(L"\n");
      continue;
    }

    line_output_options.position = position;

    wstring number_prefix = GetInitialPrefix(*buffer, position.line);
    if (!number_prefix.empty() && last_line == position.line) {
      number_prefix = wstring(number_prefix.size() - 1, L' ') + L':';
    }
    auto next_position = GetNextLine(
        *buffer, screen->columns() - number_prefix.size(), position);

    std::set<size_t> current_cursors;
    auto it = cursors.find(position.line);
    if (it != cursors.end()) {
      for (auto& c : it->second) {
        if (c >= position.column &&
            LineColumn(position.line, c) < next_position) {
          current_cursors.insert(c - position.column);
        }
      }
    }
    bool has_active_cursor =
        buffer->position() >= position && buffer->position() < next_position;

    if (!number_prefix.empty()) {
      LineModifier prefix_modifier = LineModifier::DIM;
      if (has_active_cursor ||
          (!current_cursors.empty() &&
           buffer->Read(buffer_variables::multiple_cursors()))) {
        prefix_modifier = LineModifier::CYAN;
      } else if (!current_cursors.empty()) {
        prefix_modifier = LineModifier::BLUE;
      }

      line_output_receiver = std::make_unique<WithPrefixOutputReceiver>(
          std::move(line_output_receiver), number_prefix, prefix_modifier);
      line_output_receiver->SetTabsStart(0);
    }

    std::optional<size_t> active_cursor_column;
    auto line = buffer->LineAt(position.line);

    std::unique_ptr<OutputReceiver> atomic_lines_highlighter;
    CHECK(line->contents() != nullptr);
    if (buffer->Read(buffer_variables::atomic_lines()) &&
        buffer->active_cursors()->cursors_in_line(position.line)) {
      buffer->set_last_highlighted_line(position.line);
      line_output_receiver = std::make_unique<HighlightedLineOutputReceiver>(
          std::move(line_output_receiver));
    } else if (!current_cursors.empty()) {
      LOG(INFO) << "Cursors in current line: " << current_cursors.size();
      CursorsHighlighter::Options options;
      options.delegate = std::move(line_output_receiver);
      options.columns = current_cursors;
      if (buffer->position() >= position &&
          buffer->position() < next_position) {
        options.active_cursor_input =
            min(buffer->position().column, line->size()) - position.column;
        options.active_cursor_output = &active_cursor_column;
      }
      // Any cursors past the end of the line will just be silently moved to
      // the end of the line (just for displaying).
      while (!options.columns.empty() &&
             *options.columns.rbegin() > line->size()) {
        options.columns.erase(std::prev(options.columns.end()));
        options.columns.insert(line->size());
      }
      options.multiple_cursors =
          buffer->Read(buffer_variables::multiple_cursors());

      line_output_receiver =
          std::make_unique<CursorsHighlighter>(std::move(options));
    }

    if (current_tree != root.get() &&
        position.line >= current_tree->range.begin.line &&
        position.line <= current_tree->range.end.line) {
      size_t begin = position.line == current_tree->range.begin.line
                         ? current_tree->range.begin.column
                         : 0;
      size_t end = position.line == current_tree->range.end.line
                       ? current_tree->range.end.column
                       : line->size();
      line_output_receiver = std::make_unique<ParseTreeHighlighter>(
          std::move(line_output_receiver), begin, end);
    } else if (!buffer->parse_tree()->children.empty()) {
      line_output_receiver = std::make_unique<ParseTreeHighlighterTokens>(
          std::move(line_output_receiver), root.get(), position.line,
          line->size());
    }

    line_output_options.output_receiver = line_output_receiver.get();
    line->Output(line_output_options);

    if (!buffer->Read(buffer_variables::paste_mode())) {
      const auto line_width = static_cast<size_t>(
          max(0, buffer->Read(buffer_variables::line_width())));
      AddPadding(line_width, line->end_of_line_modifiers(),
                 line_output_receiver.get());
      ShowAdditionalData(buffer.get(), *line, position, lines_to_show,
                         zoomed_out_tree.get(), line_output_receiver.get(),
                         &buffers_shown);
    }

    if (active_cursor_column.has_value()) {
      cursor_position_ =
          LineColumn(i, active_cursor_column.value() + number_prefix.size());
    }

    last_line = position.line;
    position = next_position;
  }
}

void Terminal::AdjustPosition(Screen* screen) {
  CHECK(cursor_position_.has_value());
  screen->Move(cursor_position_.value().line, cursor_position_.value().column);
}

}  // namespace editor
}  // namespace afc
