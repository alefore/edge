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
#include "src/line_marks.h"
#include "src/output_receiver.h"
#include "src/output_receiver_optimizer.h"
#include "src/parse_tree.h"
#include "src/screen_output_receiver.h"
#include "src/terminal.h"

namespace afc {
namespace editor {
namespace {
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
    std::unordered_set<const OpenBuffer*>* output_buffers_shown,
    size_t view_start_line) {
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
      additional_information = L"👻 " + mark.source_line_content->ToString();
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

  if (additional_information.empty()) {
    auto parse_tree = buffer->simplified_parse_tree();
    if (parse_tree != nullptr) {
      additional_information +=
          DrawTree(position.line, buffer->lines_size(), *parse_tree);
    }
    if (buffer->Read(buffer_variables::scrollbar()) &&
        buffer->lines_size() > lines_to_show) {
      CHECK_GE(position.line, view_start_line);
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

BufferOutputProducer::BufferOutputProducer(
    std::shared_ptr<OpenBuffer> buffer, size_t lines_shown,
    size_t columns_shown, LineColumn view_start,
    std::shared_ptr<const ParseTree> zoomed_out_tree)
    : buffer_(std::move(buffer)),
      lines_shown_(lines_shown),
      columns_shown_(columns_shown),
      view_start_(view_start),
      cursors_([=]() {
        std::map<size_t, std::set<size_t>> cursors;
        for (auto cursor : *buffer_->active_cursors()) {
          cursors[cursor.line].insert(cursor.column);
        }
        return cursors;
      }()),
      root_(buffer_->parse_tree()),
      current_tree_(buffer_->current_tree(root_.get())),
      zoomed_out_tree_(std::move(zoomed_out_tree)),
      range_(GetRangeStartingAt(view_start_)) {
  if (buffer_->Read(buffer_variables::reload_on_display())) {
    buffer_->Reload();
  }
}

void BufferOutputProducer::WriteLine(Options options) {
  if (options.active_cursor != nullptr) {
    *options.active_cursor = std::nullopt;
  }

  if (range_.begin.line >= buffer_->lines_size()) {
    options.receiver->AddString(L"\n");
    return;
  }

  auto current_cursors = GetCurrentCursors();

  std::optional<size_t> active_cursor_column;
  auto line = buffer_->LineAt(range_.begin.line);

  std::unique_ptr<OutputReceiver> atomic_lines_highlighter;
  CHECK(line->contents() != nullptr);
  if (buffer_->Read(buffer_variables::atomic_lines()) &&
      buffer_->active_cursors()->cursors_in_line(range_.begin.line)) {
    options.receiver = std::make_unique<HighlightedLineOutputReceiver>(
        std::move(options.receiver));
  } else if (!current_cursors.empty()) {
    LOG(INFO) << "Cursors in current line: " << current_cursors.size();
    CursorsHighlighterOptions cursors_highlighter_options;
    cursors_highlighter_options.delegate = std::move(options.receiver);
    cursors_highlighter_options.columns = current_cursors;
    if (range_.Contains(buffer_->position())) {
      cursors_highlighter_options.active_cursor_input =
          min(buffer_->position().column, line->size()) - range_.begin.column;
      cursors_highlighter_options.active_cursor_output = &active_cursor_column;
    }
    // Any cursors past the end of the line will just be silently moved to
    // the end of the line (just for displaying).
    while (!cursors_highlighter_options.columns.empty() &&
           *cursors_highlighter_options.columns.rbegin() > line->size()) {
      cursors_highlighter_options.columns.erase(
          std::prev(cursors_highlighter_options.columns.end()));
      cursors_highlighter_options.columns.insert(line->size());
    }
    cursors_highlighter_options.multiple_cursors =
        buffer_->Read(buffer_variables::multiple_cursors());

    options.receiver =
        NewCursorsHighlighter(std::move(cursors_highlighter_options));
  }

  if (current_tree_ != root_.get() &&
      range_.begin.line >= current_tree_->range.begin.line &&
      range_.begin.line <= current_tree_->range.end.line) {
    size_t begin = range_.begin.line == current_tree_->range.begin.line
                       ? current_tree_->range.begin.column
                       : 0;
    size_t end = range_.begin.line == current_tree_->range.end.line
                     ? current_tree_->range.end.column
                     : line->size();
    options.receiver = std::make_unique<ParseTreeHighlighter>(
        std::move(options.receiver), begin, end);
  } else if (!buffer_->parse_tree()->children.empty()) {
    options.receiver = std::make_unique<ParseTreeHighlighterTokens>(
        std::move(options.receiver), root_.get(), range_.begin.line,
        line->size());
  }

  Line::OutputOptions line_output_options;
  line_output_options.position = range_.begin;
  line_output_options.output_receiver = options.receiver.get();
  line->Output(line_output_options);

  if (!buffer_->Read(buffer_variables::paste_mode())) {
    const auto line_width = static_cast<size_t>(
        max(0, buffer_->Read(buffer_variables::line_width())));
    AddPadding(line_width, line->end_of_line_modifiers(),
               options.receiver.get());
    ShowAdditionalData(buffer_.get(), *line, range_.begin, lines_shown_,
                       zoomed_out_tree_.get(), options.receiver.get(),
                       &buffers_shown_, view_start_.line);
  }

  if (active_cursor_column.has_value() && options.active_cursor != nullptr) {
    *options.active_cursor = active_cursor_column.value();
  }

  last_line_ = range_.begin.line;
  range_ = GetRangeStartingAt(range_.end);
}

Range BufferOutputProducer::GetCurrentRange() const { return range_; }

Range BufferOutputProducer::GetRangeStartingAt(LineColumn start) const {
  Range output(start, start);
  // TODO: This is wrong: it doesn't account for multi-width characters.
  // TODO: This is wrong: it doesn't take into account line filters.
  if (start.line >= buffer_->lines_size()) {
    output.end = LineColumn(std::numeric_limits<size_t>::max());
  } else {
    output.end = LineColumn(start.line, start.column + columns_shown_);
    if (output.end.column >= buffer_->LineAt(output.end.line)->size() ||
        !buffer_->Read(buffer_variables::wrap_long_lines())) {
      output.end.line++;
      output.end.column = view_start_.column;
      if (output.end.line >= buffer_->lines_size()) {
        output.end = LineColumn(std::numeric_limits<size_t>::max());
      }
    }
  }
  return output;
}

bool BufferOutputProducer::HasActiveCursor() const {
  return GetCurrentRange().Contains(buffer_->position());
}

std::set<size_t> BufferOutputProducer::GetCurrentCursors() const {
  std::set<size_t> output;
  auto it = cursors_.find(range_.begin.line);
  if (it == cursors_.end()) {
    return output;
  }
  auto range = GetCurrentRange();
  for (auto& c : it->second) {
    if (range.Contains(LineColumn(range_.begin.line, c))) {
      output.insert(c - range_.begin.column);
    }
  }
  return output;
}

}  // namespace editor
}  // namespace afc
