#include "src/buffer_output_producer.h"

#include <glog/logging.h>

#include <algorithm>
#include <cctype>
#include <iostream>

#include "src/buffer.h"
#include "src/buffer_variables.h"
#include "src/cursors_highlighter.h"
#include "src/delegating_output_receiver.h"
#include "src/delegating_output_receiver_with_internal_modifiers.h"
#include "src/dirname.h"
#include "src/output_receiver.h"
#include "src/output_receiver_optimizer.h"
#include "src/parse_tree.h"
#include "src/screen_output_receiver.h"
#include "src/terminal.h"

namespace afc {
namespace editor {
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
                             const ParseTree* root, LineColumn position,
                             size_t largest_column_with_tree)
      : DelegatingOutputReceiverWithInternalModifiers(
            std::move(delegate), DelegatingOutputReceiverWithInternalModifiers::
                                     Preference::kExternal),
        root_(root),
        largest_column_with_tree_(largest_column_with_tree),
        line_(position.line),
        column_read_(position.column),
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
  size_t column_read_;
  std::vector<const ParseTree*> current_;
};

BufferOutputProducer::BufferOutputProducer(
    std::shared_ptr<OpenBuffer> buffer,
    std::shared_ptr<LineScrollControl::Reader> line_scroll_control_reader,
    size_t lines_shown, size_t columns_shown, size_t initial_column,
    std::shared_ptr<const ParseTree> zoomed_out_tree)
    : buffer_(std::move(buffer)),
      line_scroll_control_reader_(std::move(line_scroll_control_reader)),
      lines_shown_(lines_shown),
      columns_shown_(columns_shown),
      initial_column_(initial_column),
      root_(buffer_->parse_tree()),
      current_tree_(buffer_->current_tree(root_.get())),
      zoomed_out_tree_(std::move(zoomed_out_tree)) {
  CHECK(line_scroll_control_reader_ != nullptr);
  if (buffer_->Read(buffer_variables::reload_on_display)) {
    buffer_->Reload();
  }
}

void BufferOutputProducer::WriteLine(Options options) {
  auto optional_line = line_scroll_control_reader_->GetLine();
  if (!optional_line.has_value()) {
    return;
  }

  auto line = optional_line.value();

  if (options.active_cursor != nullptr) {
    *options.active_cursor = std::nullopt;
  }

  if (line >= buffer_->lines_size()) {
    options.receiver->AddString(L"\n");
    line_scroll_control_reader_->LineDone();
    column_ = 0;
    return;
  }

  auto range = GetRange(LineColumn(line, column_));
  std::set<size_t> current_cursors;
  for (auto& c : line_scroll_control_reader_->GetCurrentCursors()) {
    current_cursors.insert(c - column_);
  }

  std::optional<size_t> active_cursor_column;
  auto line_contents = buffer_->LineAt(line);
  auto line_size = line_contents->size();

  std::unique_ptr<OutputReceiver> atomic_lines_highlighter;
  CHECK(line_contents->contents() != nullptr);
  if (buffer_->Read(buffer_variables::atomic_lines) &&
      buffer_->active_cursors()->cursors_in_line(line)) {
    options.receiver = std::make_unique<HighlightedLineOutputReceiver>(
        std::move(options.receiver));
  } else if (!current_cursors.empty()) {
    LOG(INFO) << "Cursors in current line: " << current_cursors.size();
    CursorsHighlighterOptions cursors_highlighter_options;
    cursors_highlighter_options.delegate = std::move(options.receiver);
    cursors_highlighter_options.columns = current_cursors;
    if (range.Contains(buffer_->position())) {
      cursors_highlighter_options.active_cursor_input =
          min(buffer_->position().column, line_size) - range.begin.column;
      cursors_highlighter_options.active_cursor_output = &active_cursor_column;
    }
    // Any cursors past the end of the line will just be silently moved to
    // the end of the line (just for displaying).
    while (!cursors_highlighter_options.columns.empty() &&
           *cursors_highlighter_options.columns.rbegin() > line_size) {
      cursors_highlighter_options.columns.erase(
          std::prev(cursors_highlighter_options.columns.end()));
      cursors_highlighter_options.columns.insert(line_size);
    }
    cursors_highlighter_options.multiple_cursors =
        buffer_->Read(buffer_variables::multiple_cursors);

    options.receiver =
        NewCursorsHighlighter(std::move(cursors_highlighter_options));
  }

  if (current_tree_ != root_.get() &&
      range.begin.line >= current_tree_->range.begin.line &&
      range.begin.line <= current_tree_->range.end.line) {
    size_t begin = range.begin.line == current_tree_->range.begin.line
                       ? current_tree_->range.begin.column
                       : 0;
    size_t end = range.begin.line == current_tree_->range.end.line
                     ? current_tree_->range.end.column
                     : line_size;
    options.receiver = std::make_unique<ParseTreeHighlighter>(
        std::move(options.receiver), begin, end);
  } else if (!buffer_->parse_tree()->children.empty()) {
    options.receiver = std::make_unique<ParseTreeHighlighterTokens>(
        std::move(options.receiver), root_.get(), range.begin, line_size);
  }

  Line::OutputOptions line_output_options;
  line_output_options.position = range.begin;
  line_output_options.output_receiver = options.receiver.get();
  line_contents->Output(line_output_options);

  if (active_cursor_column.has_value() && options.active_cursor != nullptr) {
    *options.active_cursor = active_cursor_column.value();
  }

  if (range.end >= LineColumn(line, line_contents->size())) {
    column_ = initial_column_;
    line_scroll_control_reader_->LineDone();
  } else {
    CHECK_EQ(range.begin.line, range.end.line);
    column_ = range.end.column;
  }
}

Range BufferOutputProducer::GetRange(LineColumn begin) {
  // TODO: This is wrong: it doesn't account for multi-width characters.
  // TODO: This is wrong: it doesn't take into account line filters.
  if (begin.line >= buffer_->lines_size()) {
    return Range(begin, LineColumn(std::numeric_limits<size_t>::max()));
  }
  LineColumn end(begin.line, begin.column + columns_shown_);
  if (end.column < buffer_->LineAt(end.line)->size() &&
      buffer_->Read(buffer_variables::wrap_long_lines)) {
    return Range(begin, end);
  }
  end.line++;
  end.column = initial_column_;
  if (end.line >= buffer_->lines_size()) {
    end = LineColumn(std::numeric_limits<size_t>::max());
  }
  return Range(begin, end);
}

}  // namespace editor
}  // namespace afc
