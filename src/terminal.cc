#include "src/terminal.h"

#include <glog/logging.h>

#include <algorithm>
#include <cctype>
#include <iostream>

#include "src/buffer_variables.h"
#include "src/dirname.h"
#include "src/line_marks.h"
#include "src/parse_tree.h"

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

void AdvanceToNextLine(const OpenBuffer& buffer, LineColumn* position) {
  position->line++;
  position->column = buffer.Read(buffer_variables::view_start_column());
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

  auto screen_lines_positions = GetScreenLinePositions(editor_state, screen);
  if (screen_state.needs_redraw) {
    ShowBuffer(editor_state, screen, screen_lines_positions);
  }
  ShowStatus(*editor_state, screen);
  if (editor_state->status_prompt()) {
    screen->SetCursorVisibility(Screen::NORMAL);
  } else if (buffer->Read(buffer_variables::atomic_lines())) {
    screen->SetCursorVisibility(Screen::INVISIBLE);
  } else {
    screen->SetCursorVisibility(Screen::NORMAL);
    AdjustPosition(buffer, screen, screen_lines_positions);
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
      flags += L" strong";
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
      status += L"run:" + to_wstring(running) + L" ";
    }
    if (failed > 0) {
      status += L"fail:" + to_wstring(failed) + L" ";
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

class LineOutputReceiver : public Line::OutputReceiverInterface {
 public:
  LineOutputReceiver(Screen* screen) : screen_(screen) {}

  void AddCharacter(wchar_t c) override {
    if (c != L'\t') {
      screen_->WriteString(wstring(1, c));
    }
    RegisterChar(c);
  }
  void AddString(const wstring& str) override {
    for (auto& c : str) {
      AddCharacter(c);
    }
  }
  void AddModifier(LineModifier modifier) override {
    screen_->SetModifier(modifier);
  }

  size_t column() override { return column_write_; }

  void skip_columns(size_t delta) {
    CHECK_LE(delta, column_write_);
    column_write_ -= delta;
  }

 private:
  void RegisterChar(wchar_t c) {
    switch (c) {
      case L'\n':
        column_write_ = 0;
        break;
      case L'\t': {
        size_t new_value =
            8 * static_cast<size_t>(
                    1 + floor(static_cast<double>(column_write_) / 8.0));
        CHECK_GT(new_value, column_write_);
        CHECK_LE(new_value - column_write_, 8u);
        screen_->WriteString(wstring(new_value - column_write_, ' '));
        column_write_ = new_value;
      } break;
      case L'​':
        break;
      default:
        column_write_ += wcwidth(c);
    }
  }

  Screen* const screen_;
  size_t column_write_ = 0;
};

class DelegatingOutputReceiver : public Line::OutputReceiverInterface {
 public:
  DelegatingOutputReceiver(Line::OutputReceiverInterface* delegate)
      : delegate_(delegate) {}

  void AddCharacter(wchar_t c) override { delegate_->AddCharacter(c); }
  void AddString(const wstring& str) override { delegate_->AddString(str); }
  void AddModifier(LineModifier modifier) override {
    delegate_->AddModifier(modifier);
  }

  size_t column() override { return delegate_->column(); }

 private:
  Line::OutputReceiverInterface* const delegate_;
};

class HighlightedLineOutputReceiver : public DelegatingOutputReceiver {
 public:
  HighlightedLineOutputReceiver(Line::OutputReceiverInterface* delegate)
      : DelegatingOutputReceiver(delegate) {
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

// Class that merges modifiers produced at two different levels: a parent, and
// a child. For any position where the parent has any modifiers active, those
// from the child get ignored. A delegate OutputReceiverInterface is updated.
class ModifiersMerger {
 public:
  ModifiersMerger(Line::OutputReceiverInterface* delegate)
      : delegate_(delegate) {}

  void AddParentModifier(LineModifier modifier) {
    if (modifier == LineModifier::RESET) {
      if (!parent_modifiers_) {
        return;
      }
      parent_modifiers_ = false;
      delegate_->AddModifier(LineModifier::RESET);
      for (auto& m : children_modifiers_) {
        CHECK(m != LineModifier::RESET);
        delegate_->AddModifier(m);
      }
      return;
    }

    if (!parent_modifiers_) {
      if (!children_modifiers_.empty()) {
        delegate_->AddModifier(LineModifier::RESET);
      }
      parent_modifiers_ = true;
    }
    delegate_->AddModifier(modifier);
  }

  void AddChildrenModifier(LineModifier modifier) {
    if (modifier == LineModifier::RESET) {
      children_modifiers_.clear();
    } else {
      children_modifiers_.insert(modifier);
    }
    if (!parent_modifiers_) {
      delegate_->AddModifier(modifier);
    }
  }

  bool has_parent_modifiers() { return parent_modifiers_; }

 private:
  bool parent_modifiers_ = false;
  LineModifierSet children_modifiers_;
  Line::OutputReceiverInterface* const delegate_;
};

class CursorsHighlighter : public DelegatingOutputReceiver {
 public:
  struct Options {
    Line::OutputReceiverInterface* delegate;

    // A set with all the columns in the current line in which there are
    // cursors that should be drawn.
    set<size_t> columns;

    bool multiple_cursors;
  };

  explicit CursorsHighlighter(Options options)
      : DelegatingOutputReceiver(options.delegate),
        modifiers_merger_(options.delegate),
        columns_(options.columns),
        next_cursor_(columns_.begin()),
        multiple_cursors_(options.multiple_cursors) {
    CheckInvariants();
  }

  void AddCharacter(wchar_t c) {
    CheckInvariants();
    bool at_cursor =
        next_cursor_ != columns_.end() && *next_cursor_ == column_read_;
    if (at_cursor) {
      ++next_cursor_;
      CHECK(next_cursor_ == columns_.end() || *next_cursor_ > column_read_);
      modifiers_merger_.AddParentModifier(LineModifier::REVERSE);
      modifiers_merger_.AddParentModifier(
          multiple_cursors_ ? LineModifier::CYAN : LineModifier::BLUE);
    }

    DelegatingOutputReceiver::AddCharacter(c);
    if (at_cursor) {
      modifiers_merger_.AddParentModifier(LineModifier::RESET);
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
      size_t next_column = (next_cursor_ == columns_.end())
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
        CHECK(next_cursor_ != columns_.end());
        CHECK_EQ(*next_cursor_, column_read_);
        AddCharacter(str[str_pos]);
        str_pos++;
      }
      CheckInvariants();
    }
  }

  void AddModifier(LineModifier modifier) {
    modifiers_merger_.AddChildrenModifier(modifier);
  }

 private:
  void CheckInvariants() {
    if (next_cursor_ != columns_.end()) {
      CHECK_GE(*next_cursor_, column_read_);
    }
  }

  ModifiersMerger modifiers_merger_;

  const set<size_t> columns_;
  // Points to the first element in columns_ that is greater than or equal to
  // the current position.
  set<size_t>::const_iterator next_cursor_;
  const bool multiple_cursors_;
  size_t column_read_ = 0;
};

class ParseTreeHighlighter : public DelegatingOutputReceiver {
 public:
  explicit ParseTreeHighlighter(Line::OutputReceiverInterface* delegate,
                                size_t begin, size_t end)
      : DelegatingOutputReceiver(delegate), begin_(begin), end_(end) {}

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

class ParseTreeHighlighterTokens : public DelegatingOutputReceiver {
 public:
  // A Line::OutputReceiverInterface implementation that merges modifiers from
  // the syntax tree (with modifiers from the line). When modifiers from the
  // line are present, they override modifiers from the syntax tree.
  //
  // largest_column_with_tree: Position after which modifiers from the syntax
  // tree will no longer apply. This ensures that "continuation" modifiers
  // (that were active at the last character in the line) won't continue to
  // affect the padding and/or scrollbar).
  ParseTreeHighlighterTokens(Line::OutputReceiverInterface* delegate,
                             const ParseTree* root, size_t line,
                             size_t largest_column_with_tree)
      : DelegatingOutputReceiver(delegate),
        modifiers_merger_(delegate),
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
        modifiers_merger_.AddChildrenModifier(LineModifier::RESET);
      }
      DelegatingOutputReceiver::AddCharacter(c);
      return;
    }
    if (!current_.empty() && current_.back()->range.end <= position) {
      UpdateCurrent(position);
    }

    modifiers_merger_.AddChildrenModifier(LineModifier::RESET);
    if (!current_.empty() && !modifiers_merger_.has_parent_modifiers()) {
      for (auto& t : current_) {
        if (t->range.Contains(position)) {
          for (auto& modifier : t->modifiers) {
            modifiers_merger_.AddChildrenModifier(modifier);
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

  void AddModifier(LineModifier modifier) override {
    modifiers_merger_.AddParentModifier(modifier);
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
  ModifiersMerger modifiers_merger_;
  const ParseTree* root_;
  const size_t largest_column_with_tree_;
  const size_t line_;
  std::vector<const ParseTree*> current_;
  size_t column_read_ = 0;
};

std::vector<LineColumn> Terminal::GetScreenLinePositions(
    EditorState* editor_state, Screen* screen) {
  std::vector<LineColumn> output;

  OpenBuffer* buffer = editor_state->current_buffer()->second.get();
  if (screen->lines() <= 0) {
    return {};
  }
  size_t lines_to_show = static_cast<size_t>(screen->lines()) - 1;

  LineColumn position(
      static_cast<size_t>(
          max(0, buffer->Read(buffer_variables::view_start_line()))),
      buffer->Read(buffer_variables::view_start_column()));

  size_t width = screen->columns() - GetInitialPrefixSize(*buffer);
  while (output.size() < lines_to_show) {
    if (position.line >= buffer->lines_size()) {
      output.push_back(position);
      continue;
    }
    if (!buffer->IsLineFiltered(position.line)) {
      AdvanceToNextLine(*buffer, &position);
      continue;
    }

    output.push_back(position);

    position.column += width;
    if (position.column >= buffer->LineAt(position.line)->size() ||
        !buffer->Read(buffer_variables::wrap_long_lines())) {
      AdvanceToNextLine(*buffer, &position);
    }
  }
  return output;
}

void Terminal::ShowBuffer(
    const EditorState* editor_state, Screen* screen,
    const std::vector<LineColumn>& screen_line_positions) {
  const shared_ptr<OpenBuffer> buffer = editor_state->current_buffer()->second;
  size_t lines_to_show = static_cast<size_t>(screen->lines()) - 1;
  CHECK_EQ(screen_line_positions.size(), lines_to_show);

  screen->Move(0, 0);

  LineOutputReceiver screen_adapter(screen);
  auto line_output_receiver =
      std::make_unique<OutputReceiverOptimizer>(&screen_adapter);

  buffer->set_last_highlighted_line(-1);

  // Key is line number.
  std::map<size_t, std::set<size_t>> cursors;
  for (auto cursor : *buffer->active_cursors()) {
    cursors[cursor.line].insert(cursor.column);
  }

  auto root = buffer->parse_tree();
  auto current_tree = buffer->current_tree(root.get());

  Line::OutputOptions line_output_options;
  line_output_options.editor_state = editor_state;
  line_output_options.buffer = buffer.get();
  line_output_options.lines_to_show = lines_to_show;
  line_output_options.paste_mode = buffer->Read(buffer_variables::paste_mode());

  buffer->set_lines_for_zoomed_out_tree(lines_to_show);
  auto zoomed_out_tree = buffer->zoomed_out_tree();
  line_output_options.full_file_parse_tree = zoomed_out_tree.get();

  std::unordered_set<const OpenBuffer*> buffers_shown;
  line_output_options.output_buffers_shown = &buffers_shown;

  size_t last_line = std::numeric_limits<size_t>::max();
  for (size_t i = 0; i < screen_line_positions.size(); i++) {
    auto position = screen_line_positions[i];
    auto next_position = i + 1 < screen_line_positions.size()
                             ? screen_line_positions[i + 1]
                             : LineColumn(std::numeric_limits<size_t>::max());
    if (position.line >= buffer->lines_size()) {
      line_output_receiver->AddString(L"\n");
      continue;
    }

    line_output_options.position = position;
    line_output_options.output_receiver = line_output_receiver.get();
    screen_adapter.skip_columns(line_output_receiver->column());
    CHECK_EQ(line_output_options.output_receiver->column(), 0u);

    wstring number_prefix = GetInitialPrefix(*buffer, position.line);
    if (!number_prefix.empty() && last_line == position.line) {
      number_prefix = wstring(number_prefix.size() - 1, L' ') + L':';
    }
    line_output_options.line_width =
        buffer->Read(buffer_variables::line_width());
    line_output_options.width = screen->columns();

    auto current_cursors = cursors.find(position.line);
    line_output_options.has_active_cursor =
        (buffer->position() >= position &&
         buffer->position() < next_position) ||
        (current_cursors != cursors.end() &&
         buffer->Read(buffer_variables::multiple_cursors()));
    line_output_options.has_cursor = current_cursors != cursors.end();

    if (!number_prefix.empty()) {
      if (line_output_options.has_active_cursor) {
        line_output_options.output_receiver->AddModifier(LineModifier::CYAN);
      } else if (line_output_options.has_cursor) {
        line_output_options.output_receiver->AddModifier(LineModifier::BLUE);
      } else {
        line_output_options.output_receiver->AddModifier(LineModifier::DIM);
      }
      line_output_options.output_receiver->AddString(number_prefix);
      line_output_options.output_receiver->AddModifier(LineModifier::RESET);
      CHECK_EQ(line_output_receiver->column(), number_prefix.size());

      line_output_options.width -= number_prefix.size();
      screen_adapter.skip_columns(number_prefix.size());
    }

    std::unique_ptr<Line::OutputReceiverInterface> cursors_highlighter;

    auto line = buffer->LineAt(position.line);

    std::unique_ptr<Line::OutputReceiverInterface> atomic_lines_highlighter;
    CHECK(line->contents() != nullptr);
    if (buffer->Read(buffer_variables::atomic_lines()) &&
        buffer->active_cursors()->cursors_in_line(position.line)) {
      buffer->set_last_highlighted_line(position.line);
      atomic_lines_highlighter =
          std::make_unique<HighlightedLineOutputReceiver>(
              line_output_options.output_receiver);
      line_output_options.output_receiver = atomic_lines_highlighter.get();
    } else if (current_cursors != cursors.end()) {
      LOG(INFO) << "Cursors in current line: "
                << current_cursors->second.size();
      CursorsHighlighter::Options options;
      options.delegate = line_output_options.output_receiver;
      options.columns = current_cursors->second;
      // Any cursors past the end of the line will just be silently moved to
      // the end of the line (just for displaying).
      while (!options.columns.empty() &&
             *options.columns.rbegin() > line->size()) {
        options.columns.erase(std::prev(options.columns.end()));
        options.columns.insert(line->size());
      }
      options.multiple_cursors =
          buffer->Read(buffer_variables::multiple_cursors());

      cursors_highlighter = std::make_unique<CursorsHighlighter>(options);
      line_output_options.output_receiver = cursors_highlighter.get();
    }

    std::unique_ptr<Line::OutputReceiverInterface> parse_tree_highlighter;
    if (current_tree != root.get() &&
        position.line >= current_tree->range.begin.line &&
        position.line <= current_tree->range.end.line) {
      size_t begin = position.line == current_tree->range.begin.line
                         ? current_tree->range.begin.column
                         : 0;
      size_t end = position.line == current_tree->range.end.line
                       ? current_tree->range.end.column
                       : line->size();
      parse_tree_highlighter = std::make_unique<ParseTreeHighlighter>(
          line_output_options.output_receiver, begin, end);
      line_output_options.output_receiver = parse_tree_highlighter.get();
    } else if (!buffer->parse_tree()->children.empty()) {
      parse_tree_highlighter = std::make_unique<ParseTreeHighlighterTokens>(
          line_output_options.output_receiver, root.get(), position.line,
          line->size());
      line_output_options.output_receiver = parse_tree_highlighter.get();
    }

    line->Output(line_output_options);

    // Need to do this for atomic lines, since they override the Reset
    // modifier with Reset + Reverse.
    line_output_receiver->AddModifier(LineModifier::RESET);
    last_line = position.line;
  }
}

void Terminal::AdjustPosition(
    const shared_ptr<OpenBuffer> buffer, Screen* screen,
    const std::vector<LineColumn>& screen_line_positions) {
  CHECK(!screen_line_positions.empty());
  LineColumn position;
  CHECK_GT(buffer->contents()->size(), 0u);
  if (position.line > buffer->lines_size() - 1) {
    position = LineColumn(buffer->lines_size() - 1);
  } else {
    position = buffer->position();
  }

  auto screen_line = std::upper_bound(screen_line_positions.begin(),
                                      screen_line_positions.end(), position);
  if (screen_line != screen_line_positions.begin()) {
    --screen_line;
  }
  auto pos_x = GetCurrentColumn(buffer.get());
  pos_x -= min(pos_x, screen_line->column);
  pos_x += GetInitialPrefixSize(*buffer);
  size_t pos_y = std::distance(screen_line_positions.begin(), screen_line);
  screen->Move(pos_y, min(static_cast<size_t>(screen->columns()) - 1, pos_x));
}

}  // namespace editor
}  // namespace afc
