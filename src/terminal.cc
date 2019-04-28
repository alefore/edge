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
  if (editor_state->status_prompt()) {
    screen->SetCursorVisibility(Screen::NORMAL);
  } else if (buffer == nullptr ||
             buffer->Read(buffer_variables::atomic_lines) ||
             !cursor_position_.has_value()) {
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
  wstring status;
  auto buffer = editor_state.current_buffer();
  if (buffer != nullptr && !editor_state.is_status_warning()) {
    const auto modifiers = editor_state.modifiers();
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
            it.second->Read(buffer_variables::name));
        size_t progress =
            it.second->Read(buffer_variables::progress) % (4 + 2 * name.size());
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
                (buffer->Read(buffer_variables::multiple_cursors)
                     ? wstring(L"CURSORS")
                     : wstring(L"cursors")) +
                L":" + to_wstring(active_cursors) + L" ";
    }

    auto flags = buffer->Flags();
    if (editor_state.repetitions() != 1) {
      flags.insert({to_wstring(editor_state.repetitions()), L""});
    }
    if (modifiers.default_direction == BACKWARDS) {
      flags.insert({L"REVERSE", L""});
    } else if (modifiers.direction == BACKWARDS) {
      flags.insert({L"reverse", L""});
    }

    if (modifiers.default_insertion == Modifiers::REPLACE) {
      flags.insert({L"REPLACE", L""});
    } else if (modifiers.insertion == Modifiers::REPLACE) {
      flags.insert({L"replace", L""});
    }

    if (modifiers.strength == Modifiers::Strength::kStrong) {
      flags.insert({L"💪", L""});
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
      flags[L"St:"] = structure;
    }

    if (!flags.empty()) {
      status += L"  " + OpenBuffer::FlagsToString(std::move(flags));
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
  return buffer->Read(buffer_variables::name);
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

void Terminal::ShowBuffer(EditorState* editor_state, Screen* screen) {
  screen->Move(0, 0);

  cursor_position_ = std::nullopt;

  size_t lines_to_show = static_cast<size_t>(screen->lines()) - 1;
  auto buffer_tree = editor_state->buffer_tree();
  buffer_tree->SetSize(lines_to_show, screen->columns());
  auto output_producer = editor_state->buffer_tree()->CreateOutputProducer();
  for (size_t line = 0; line < lines_to_show; line++) {
    OutputProducer::Options options;
    options.receiver = std::make_unique<OutputReceiverOptimizer>(
        NewScreenOutputReceiver(screen));

    std::optional<size_t> active_cursor_column;
    options.active_cursor = &active_cursor_column;

    output_producer->WriteLine(std::move(options));

    if (active_cursor_column.has_value()) {
      cursor_position_ = LineColumn(line, active_cursor_column.value());
    }
  }
}

void Terminal::AdjustPosition(Screen* screen) {
  CHECK(cursor_position_.has_value());
  screen->Move(cursor_position_.value().line, cursor_position_.value().column);
}

}  // namespace editor
}  // namespace afc
