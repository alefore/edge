#include "terminal.h"

#include <algorithm>
#include <cassert>
#include <cctype>
#include <iostream>

#include "line_marks.h"

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
constexpr int Terminal::CTRL_E;
constexpr int Terminal::CTRL_L;
constexpr int Terminal::CTRL_U;
constexpr int Terminal::CTRL_K;
constexpr int Terminal::CHAR_EOF;

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
  if (buffer->read_bool_variable(OpenBuffer::variable_reload_on_display())) {
    buffer->Reload(editor_state);
  }
  size_t line = min(buffer->position().line, buffer->contents()->size() - 1);
  size_t margin_lines =
      max(buffer->Read(OpenBuffer::variable_margin_lines()), 0);
  margin_lines = min(margin_lines, static_cast<size_t>(screen_lines) / 2 - 1);
  size_t view_start = static_cast<size_t>(
      max(0, buffer->Read(OpenBuffer::variable_view_start_line())));
  if (view_start > line - min(margin_lines, line)) {
    buffer->set_int_variable(OpenBuffer::variable_view_start_line(),
        line - min(margin_lines, line));
    editor_state->ScheduleRedraw();
  } else if (view_start + screen_lines - 1
                 <= min(buffer->lines_size() - 1, line + margin_lines)) {
    buffer->set_int_variable(OpenBuffer::variable_view_start_line(),
        min(buffer->lines_size() - 1, line + margin_lines) - screen_lines + 2);
    editor_state->ScheduleRedraw();
  }

  size_t desired_start_column = buffer->current_position_col()
      - min(buffer->current_position_col(), screen->columns() - 1);
  if (static_cast<size_t>(
              max(0, buffer->Read(OpenBuffer::variable_view_start_column())))
          != desired_start_column) {
    buffer->set_int_variable(OpenBuffer::variable_view_start_column(),
                             desired_start_column);
    editor_state->ScheduleRedraw();
  }
  if (buffer->read_bool_variable(OpenBuffer::variable_atomic_lines())
      && buffer->last_highlighted_line() != buffer->position().line) {
    editor_state->ScheduleRedraw();
  }

  if (screen_state.needs_redraw) {
    ShowBuffer(editor_state, screen);
  }
  ShowStatus(*editor_state, screen);
  if (editor_state->status_prompt()) {
    screen->SetCursorVisibility(Screen::NORMAL);
  } else if (buffer->read_bool_variable(OpenBuffer::variable_atomic_lines())) {
    screen->SetCursorVisibility(Screen::INVISIBLE);
  } else {
    screen->SetCursorVisibility(Screen::NORMAL);
    AdjustPosition(buffer, screen);
  }
  screen->Refresh();
  screen->Flush();
  editor_state->set_visible_lines(static_cast<size_t>(screen_lines - 1));
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
    status += L" of " + to_wstring(buffer->contents()->size()) + L", "
        + to_wstring(buffer->current_position_col() + 1);

    status += L"] ";

    for (auto& it : *editor_state.buffers()) {
      if (it.second->ShouldDisplayProgress()) {
        static const std::vector<wstring> begin = {L"◜", L" ", L" ", L"◟"};
        static const std::vector<wstring> end = {L" ", L"◝", L"◞", L" "};
        int progress = it.second->Read(OpenBuffer::variable_progress()) % 4;
        auto name = it.second->name();
        if (name.size() > 2 && name[0] == L'$' && name[1] == L' ') {
          name = name.substr(2, 1);
        }
        status += begin[progress] + (name.empty() ? L'…' : name[0])
            + end[progress] + L" ";
      }
    }

    auto marks_text = buffer->GetLineMarksText(editor_state);
    if (!marks_text.empty()) {
      status += marks_text + L" ";
    }

    auto active_cursors = buffer->active_cursors()->size();
    if (active_cursors != 1) {
      status += L" "
          + (buffer->read_bool_variable(buffer->variable_multiple_cursors())
                 ? wstring(L"CURSORS") : wstring(L"cursors"))
          + L":" + to_wstring(active_cursors) + L" ";
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

    switch (modifiers.strength) {
      case Modifiers::WEAK:
        flags += L" w";
        break;
      case Modifiers::VERY_WEAK:
        flags += L" W";
        break;
      case Modifiers::STRONG:
        flags += L" s";
        break;
      case Modifiers::VERY_STRONG:
        flags += L" S";
        break;
      case Modifiers::DEFAULT:
        break;
    }

    wstring structure;
    switch (editor_state.structure()) {
      case CHAR:
        break;
      case WORD:
        structure = L"word";
        break;
      case LINE:
        structure = L"line";
        break;
      case MARK:
        structure = L"mark";
        break;
      case PAGE:
        structure = L"page";
        break;
      case SEARCH:
        structure = L"search";
        break;
      case BUFFER:
        structure = L"buffer";
        break;
      case CURSOR:
        structure = L"cursor";
        break;
      case TREE:
        structure = L"tree<" + to_wstring(buffer->tree_depth()) + L">";
        break;
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

  size_t status_column = status.size();
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

wstring Terminal::GetBufferContext(
    const EditorState& editor_state,
    const shared_ptr<OpenBuffer>& buffer) {
  auto marks = buffer->GetLineMarks(editor_state);
  auto current_line_marks = marks->find(buffer->position().line);
  if (current_line_marks != marks->end()) {
    auto mark = current_line_marks->second;
    auto source = editor_state.buffers()->find(mark.source);
    if (source != editor_state.buffers()->end()
        && source->second->contents()->size() > mark.source_line) {
      return source->second->contents()->at(mark.source_line)->ToString();
    }
  }
  return buffer->name();
}

class LineOutputReceiver : public Line::OutputReceiverInterface {
 public:
  LineOutputReceiver(Screen* screen) : screen_(screen) {}

  void AddCharacter(wchar_t c) {
    screen_->WriteString(wstring(1, c));
  }
  void AddString(const wstring& str) {
    screen_->WriteString(str);
  }
  void AddModifier(LineModifier modifier) {
    screen_->SetModifier(modifier);
  }
 private:
  Screen* const screen_;
};

class HighlightedLineOutputReceiver : public Line::OutputReceiverInterface {
 public:
  HighlightedLineOutputReceiver(Line::OutputReceiverInterface* delegate)
      : delegate_(delegate) {
    delegate_->AddModifier(LineModifier::REVERSE);
  }

  void AddCharacter(wchar_t c) { delegate_->AddCharacter(c); }
  void AddString(const wstring& str) { delegate_->AddString(str); }
  void AddModifier(LineModifier modifier) {
    switch (modifier) {
      case LineModifier::RESET:
        delegate_->AddModifier(LineModifier::RESET);
        delegate_->AddModifier(LineModifier::REVERSE);
        break;
      default:
        delegate_->AddModifier(modifier);
    }
  }
 private:
  Line::OutputReceiverInterface* const delegate_;
};

// Class that merges modifiers produced at two different levels: a parent, and a
// child. For any position where the parent has any modifiers active, those from
// the child get ignored. A delegate OutputReceiverInterface is updated.
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

  bool has_parent_modifiers() {
    return parent_modifiers_;
  }

 private:
  bool parent_modifiers_ = false;
  LineModifierSet children_modifiers_;
  Line::OutputReceiverInterface* const delegate_;
};

class ReceiverTrackingPosition : public Line::OutputReceiverInterface {
 public:
  ReceiverTrackingPosition(Line::OutputReceiverInterface* delegate)
      : delegate_(delegate) {}

  size_t position() const { return position_; }

  void AddCharacter(wchar_t c) override {
    position_++;
    delegate_->AddCharacter(c);
  }

  void AddString(const wstring& str) override {
    position_+= str.size();
    delegate_->AddString(str);
  }

  void AddModifier(LineModifier modifier) override {
    delegate_->AddModifier(modifier);
  }

 private:
  Line::OutputReceiverInterface* const delegate_;
  size_t position_ = 0;
};

class CursorsHighlighter : public Line::OutputReceiverInterface {
 public:
  struct Options {
    Line::OutputReceiverInterface* delegate;

    // A set with all the columns in the current line in which there are
    // cursors that should be drawn. If the active cursor (i.e., the one exposed
    // to the terminal) is in the line being outputted, its column should not be
    // included (since we shouldn't do anything special when outputting its
    // corresponding character: the terminal will take care of drawing the
    // cursor).
    set<size_t> columns;

    bool multiple_cursors;
  };

  explicit CursorsHighlighter(Options options)
      : delegate_(options.delegate),
        modifiers_merger_(&delegate_),
        columns_(options.columns),
        next_cursor_(columns_.begin()),
        multiple_cursors_(options.multiple_cursors) {
    CheckInvariants();
  }

  void AddCharacter(wchar_t c) {
    CheckInvariants();
    bool at_cursor =
        next_cursor_ != columns_.end() && *next_cursor_ == delegate_.position();
    if (at_cursor) {
      ++next_cursor_;
      CHECK(next_cursor_ == columns_.end()
            || *next_cursor_ > delegate_.position());
      modifiers_merger_.AddParentModifier(LineModifier::REVERSE);
      modifiers_merger_.AddParentModifier(
          multiple_cursors_ ? LineModifier::CYAN : LineModifier::BLUE);
    }

    delegate_.AddCharacter(c);
    if (at_cursor) {
      modifiers_merger_.AddParentModifier(LineModifier::RESET);
    }
    CheckInvariants();
  }

  void AddString(const wstring& str) {
    size_t str_pos = 0;
    while (str_pos < str.size()) {
      CheckInvariants();
      DCHECK_GE(delegate_.position(), str_pos);

      // Compute the position of the next cursor relative to the start of this
      // string.
      size_t next_column = (next_cursor_ == columns_.end())
          ? str.size() : *next_cursor_ + str_pos - delegate_.position();
      if (next_column > str_pos) {
        size_t len = next_column - str_pos;
        delegate_.AddString(str.substr(str_pos, len));
        str_pos += len;
      }

      CheckInvariants();

      if (str_pos < str.size()) {
        CHECK(next_cursor_ != columns_.end());
        CHECK_EQ(*next_cursor_, delegate_.position());
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
      CHECK_GE(*next_cursor_, delegate_.position());
    }
  }

  ReceiverTrackingPosition delegate_;
  ModifiersMerger modifiers_merger_;

  const set<size_t> columns_;
  // Points to the first element in columns_ that is greater than or equal to
  // the current position.
  set<size_t>::const_iterator next_cursor_;
  const bool multiple_cursors_;
};

class ParseTreeHighlighter : public Line::OutputReceiverInterface {
 public:
  explicit ParseTreeHighlighter(
      Line::OutputReceiverInterface* delegate, size_t begin, size_t end)
      : delegate_(delegate), begin_(begin), end_(end) {}

  void AddCharacter(wchar_t c) override {
    size_t position = delegate_.position();
    // TODO: Optimize: Don't add it for each character, just at the start.
    if (begin_ <= position && position < end_) {
      AddModifier(LineModifier::BLUE);
    }

    delegate_.AddCharacter(c);

    // TODO: Optimize: Don't add it for each character, just at the end.
    if (c != L'\n') {
      AddModifier(LineModifier::RESET);
    }
  }

  void AddString(const wstring& str) override {
    // TODO: Optimize.
    if (str == L"\n") {
      delegate_.AddString(str);
      return;
    }
    for (auto& c : str) { AddCharacter(c); }
  }

  void AddModifier(LineModifier modifier) override {
    delegate_.AddModifier(modifier);
  }

 private:
  ReceiverTrackingPosition delegate_;
  const size_t begin_;
  const size_t end_;
};

class ParseTreeHighlighterTokens : public Line::OutputReceiverInterface {
 public:
  explicit ParseTreeHighlighterTokens(
      Line::OutputReceiverInterface* delegate, const ParseTree* root,
      size_t line)
      : delegate_(delegate),
        modifiers_merger_(&delegate_),
        root_(root),
        line_(line),
        current_({root}) {
    UpdateCurrent(LineColumn(line_, delegate_.position()));
  }

  void AddCharacter(wchar_t c) override {
    LineColumn position(line_, delegate_.position());
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
    delegate_.AddCharacter(c);
  }

  void AddString(const wstring& str) override {
    // TODO: Optimize.
    if (str == L"\n") {
      delegate_.AddString(str);
      return;
    }
    for (auto& c : str) { AddCharacter(c); }
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
          position,
          [](const LineColumn& position, const ParseTree& candidate) {
            return position < candidate.range.end;
          });
      if (it == current_.back()->children.end()) {
        return;
      }
      current_.push_back(&*it);
    }
  }

  ReceiverTrackingPosition delegate_;
  // Keeps track of the modifiers coming from the parent, so as to not lose that
  // information when we reset our own.
  ModifiersMerger modifiers_merger_;
  const ParseTree* root_;
  const size_t line_;
  std::vector<const ParseTree*> current_;
};

void Terminal::ShowBuffer(const EditorState* editor_state, Screen* screen) {
  const shared_ptr<OpenBuffer> buffer = editor_state->current_buffer()->second;
  screen->Move(0, 0);

  LineOutputReceiver screen_adapter(screen);
  std::unique_ptr<Line::OutputReceiverInterface> line_output_receiver(
      new OutputReceiverOptimizer(&screen_adapter));

  size_t lines_to_show = static_cast<size_t>(screen->lines());
  size_t current_line = static_cast<size_t>(
      max(0, buffer->Read(OpenBuffer::variable_view_start_line())));
  size_t lines_shown = 0;
  buffer->set_last_highlighted_line(-1);

  // Key is line number.
  std::map<size_t, std::set<size_t>> cursors;
  for (auto cursor : *buffer->active_cursors()) {
    if (cursor != buffer->position()) {
      cursors[cursor.line].insert(cursor.column);
    }
  }

  auto root = buffer->parse_tree();
  auto current_tree = buffer->current_tree(root.get());
  while (lines_shown < lines_to_show) {
    if (current_line >= buffer->lines_size()) {
      line_output_receiver->AddString(L"\n");
      lines_shown++;
      continue;
    }
    if (!buffer->IsLineFiltered(current_line)) {
      current_line ++;
      continue;
    }

    Line::OutputReceiverInterface* receiver = line_output_receiver.get();
    std::unique_ptr<Line::OutputReceiverInterface> atomic_lines_highlighter;

    auto current_cursors = cursors.find(current_line);
    std::unique_ptr<Line::OutputReceiverInterface> cursors_highlighter;

    lines_shown++;
    auto line = buffer->LineAt(current_line);
    CHECK(line->contents() != nullptr);
    if (current_line == buffer->position().line
        && buffer->read_bool_variable(OpenBuffer::variable_atomic_lines())) {
      buffer->set_last_highlighted_line(current_line);
      atomic_lines_highlighter.reset(
          new HighlightedLineOutputReceiver(receiver));
      receiver = atomic_lines_highlighter.get();
    } else if (current_cursors != cursors.end()) {
      LOG(INFO) << "Cursors in current line: "
                << current_cursors->second.size();
      CursorsHighlighter::Options options;
      options.delegate = receiver;
      options.columns = current_cursors->second;
      if (current_line == buffer->current_position_line()) {
        options.columns.erase(buffer->current_position_col());
      }
      // Any cursors past the end of the line will just be silently moved to the
      // end of the line (just for displaying).
      unsigned line_length = buffer->LineAt(current_line)->size();
      while (!options.columns.empty() &&
             *options.columns.rbegin() > line_length) {
        options.columns.erase(std::prev(options.columns.end()));
        options.columns.insert(line_length);
      }
      options.multiple_cursors =
          buffer->read_bool_variable(buffer->variable_multiple_cursors());
      cursors_highlighter.reset(new CursorsHighlighter(options));
      receiver = cursors_highlighter.get();
    }

    std::unique_ptr<Line::OutputReceiverInterface> parse_tree_highlighter;
    if (current_tree != root.get()
        && current_line >= current_tree->range.begin.line
        && current_line <= current_tree->range.end.line) {
      size_t begin = current_line == current_tree->range.begin.line
                         ? current_tree->range.begin.column
                         : 0;
      size_t end = current_line == current_tree->range.end.line
                       ? current_tree->range.end.column
                       : line->size();
      parse_tree_highlighter.reset(
          new ParseTreeHighlighter(receiver, begin, end));
      receiver = parse_tree_highlighter.get();
    } else if (!buffer->parse_tree()->children.empty()) {
      parse_tree_highlighter.reset(new ParseTreeHighlighterTokens(
          receiver, root.get(), current_line));
      receiver = parse_tree_highlighter.get();
    }

    line->Output(editor_state, buffer, current_line, receiver,
                 screen->columns());
    // Need to do this for atomic lines, since they override the Reset modifier
    // with Reset + Reverse.
    line_output_receiver->AddModifier(LineModifier::RESET);
    current_line ++;
  }
}

void Terminal::AdjustPosition(
    const shared_ptr<OpenBuffer> buffer, Screen* screen) {
  size_t position_line = min(buffer->position().line, buffer->lines_size() - 1);
  size_t line_length;
  if (buffer->lines_size() == 0) {
    line_length = 0;
  } else if (buffer->position().line >= buffer->lines_size()) {
    line_length = buffer->contents()->back()->size();
  } else if (!buffer->IsLineFiltered(buffer->position().line)) {
    line_length = 0;
  } else {
    line_length = buffer->LineAt(position_line)->size();
  }
  size_t pos_x = min(static_cast<size_t>(screen->columns()) - 1, line_length);
  if (buffer->position().line < buffer->lines_size()) {
    pos_x = min(pos_x, buffer->position().column);
  }

  size_t pos_y = 0;
  for (size_t line = static_cast<size_t>(
           max(0, buffer->Read(OpenBuffer::variable_view_start_line())));
       line < position_line; line++) {
    if (buffer->IsLineFiltered(line)) {
      pos_y++;
    }
  }
  screen->Move(pos_y, pos_x);
}

}  // namespace afc
}  // namespace editor
