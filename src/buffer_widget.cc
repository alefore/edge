#include "src/buffer_widget.h"

#include <glog/logging.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <iostream>

#include "src/buffer.h"
#include "src/buffer_metadata_output_producer.h"
#include "src/buffer_output_producer.h"
#include "src/buffer_variables.h"
#include "src/line_scroll_control.h"
#include "src/vertical_split_output_producer.h"
#include "src/widget.h"
#include "src/wstring.h"

namespace afc {
namespace editor {
namespace {
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

size_t GetDesiredViewStartColumn(OpenBuffer* buffer) {
  if (buffer->Read(buffer_variables::wrap_long_lines())) {
    return 0;
  }
  size_t effective_size = 80;  // TODO: This is bogus.
  effective_size -=
      min(effective_size, /* GetInitialPrefixSize(*buffer) */ 3ul);
  size_t column = GetCurrentColumn(buffer);
  return column - min(column, effective_size);
}
}  // namespace

BufferWidget::BufferWidget(ConstructorAccessTag,
                           std::weak_ptr<OpenBuffer> buffer)
    : leaf_(buffer) {}

/* static */
std::unique_ptr<BufferWidget> BufferWidget::New(
    std::weak_ptr<OpenBuffer> buffer) {
  return std::make_unique<BufferWidget>(ConstructorAccessTag(), buffer);
}

wstring BufferWidget::Name() const {
  auto buffer = Lock();
  return buffer == nullptr ? L"" : buffer->Read(buffer_variables::name());
}

wstring BufferWidget::ToString() const {
  auto buffer = leaf_.lock();
  return L"[buffer tree leaf" +
         (buffer == nullptr ? L"nullptr"
                            : buffer->Read(buffer_variables::name())) +
         L"]";
}

BufferWidget* BufferWidget::GetActiveLeaf() { return this; }

class LineNumberOutputProducer : public OutputProducer {
 public:
  static size_t PrefixWidth(size_t lines_size) {
    return 1 + std::to_wstring(lines_size).size();
  }

  LineNumberOutputProducer(
      std::shared_ptr<OpenBuffer> buffer,
      std::unique_ptr<LineScrollControl::Reader> line_scroll_control_reader)
      : width_(PrefixWidth(buffer->lines_size())),
        buffer_(std::move(buffer)),
        line_scroll_control_reader_(std::move(line_scroll_control_reader)) {}

  void WriteLine(Options options) override {
    auto line = line_scroll_control_reader_->GetLine();
    if (line.has_value() && line.value() >= buffer_->lines_size()) {
      return;  // Happens when the buffer is smaller than the screen.
    }

    std::wstring number =
        line.has_value() ? std::to_wstring(line.value() + 1) : L"↪";
    CHECK_LE(number.size(), width_ - 1);
    std::wstring padding(width_ - number.size() - 1, L' ');
    if (!line.has_value() ||
        line_scroll_control_reader_->GetCurrentCursors().empty()) {
      options.receiver->AddModifier(LineModifier::DIM);
    } else if (line_scroll_control_reader_->HasActiveCursor() ||
               buffer_->Read(buffer_variables::multiple_cursors())) {
      options.receiver->AddModifier(LineModifier::CYAN);
      options.receiver->AddModifier(LineModifier::BOLD);
    } else {
      options.receiver->AddModifier(LineModifier::BLUE);
    }
    options.receiver->AddString(padding + number + L':');

    if (line.has_value()) {
      line_scroll_control_reader_->LineDone();
    }
  }

  size_t width() const { return width_; }

 private:
  const size_t width_;
  const std::shared_ptr<OpenBuffer> buffer_;
  const std::unique_ptr<LineScrollControl::Reader> line_scroll_control_reader_;
};

std::unique_ptr<OutputProducer> BufferWidget::CreateOutputProducer() {
  auto buffer = Lock();
  if (buffer == nullptr) {
    return nullptr;
  }

  bool paste_mode = buffer->Read(buffer_variables::paste_mode());
  size_t buffer_columns =
      columns_ -
      (paste_mode
           ? 0
           : LineNumberOutputProducer::PrefixWidth(buffer->lines_size()));
  if (!buffer->Read(buffer_variables::paste_mode())) {
    buffer_columns =
        min(buffer_columns,
            static_cast<size_t>(buffer->Read(buffer_variables::line_width())));
  }
  auto line_scroll_control = LineScrollControl::New(buffer, view_start_.line);

  auto buffer_output_producer = std::make_unique<BufferOutputProducer>(
      buffer, line_scroll_control->NewReader(), lines_, buffer_columns,
      view_start_.column, zoomed_out_tree_);
  if (paste_mode) {
    return buffer_output_producer;
  }

  std::vector<VerticalSplitOutputProducer::Column> columns(3);

  auto line_numbers = std::make_unique<LineNumberOutputProducer>(
      buffer, line_scroll_control->NewReader());

  columns[0].width = line_numbers->width();
  columns[0].producer = std::move(line_numbers);

  columns[1].width = buffer->Read(buffer_variables::line_width());
  columns[1].producer = std::move(buffer_output_producer);

  columns[2].producer = std::make_unique<BufferMetadataOutputProducer>(
      buffer, line_scroll_control->NewReader(), lines_, view_start_.line,
      zoomed_out_tree_);

  return std::make_unique<VerticalSplitOutputProducer>(std::move(columns), 1);
}

void BufferWidget::SetSize(size_t lines, size_t columns) {
  lines_ = lines;
  columns_ = columns;
  RecomputeData();
}

size_t BufferWidget::lines() const { return lines_; }
size_t BufferWidget::columns() const { return columns_; }

size_t BufferWidget::MinimumLines() {
  auto buffer = Lock();
  return buffer == nullptr
             ? 0
             : max(0,
                   buffer->Read(buffer_variables::buffer_list_context_lines()));
}

LineColumn BufferWidget::view_start() const { return view_start_; }

std::shared_ptr<OpenBuffer> BufferWidget::Lock() const { return leaf_.lock(); }

void BufferWidget::SetBuffer(std::weak_ptr<OpenBuffer> buffer) {
  leaf_ = std::move(buffer);
  RecomputeData();
}

void BufferWidget::RecomputeData() {
  auto buffer = leaf_.lock();
  if (buffer == nullptr) {
    view_start_ = LineColumn();
    zoomed_out_tree_ = nullptr;
    return;
  }

  size_t line = min(buffer->position().line, buffer->contents()->size() - 1);
  size_t margin_lines = min(
      lines_ / 2 - 1,
      max(static_cast<size_t>(ceil(
              buffer->Read(buffer_variables::margin_lines_ratio()) * lines_)),
          static_cast<size_t>(
              max(buffer->Read(buffer_variables::margin_lines()), 0))));

  if (view_start_.line > line - min(margin_lines, line) &&
      (buffer->child_pid() != -1 || buffer->fd() == -1)) {
    view_start_.line = line - min(margin_lines, line);
    // editor_state->ScheduleRedraw();
  } else if (view_start_.line + lines_ <=
             min(buffer->lines_size(), line + margin_lines)) {
    view_start_.line =
        min(buffer->lines_size() - 1, line + margin_lines) - lines_ + 1;
    // editor_state->ScheduleRedraw();
  }

  view_start_.column = GetDesiredViewStartColumn(buffer.get());

  auto simplified_parse_tree = buffer->simplified_parse_tree();
  if (lines_ > 0 && simplified_parse_tree != nullptr &&
      simplified_parse_tree != simplified_parse_tree_) {
    zoomed_out_tree_ = std::make_shared<ParseTree>(ZoomOutTree(
        *buffer->simplified_parse_tree(), buffer->lines_size(), lines_));
    simplified_parse_tree_ = simplified_parse_tree;
  }
}

}  // namespace editor
}  // namespace afc
