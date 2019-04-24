#include "src/buffer_widget.h"

#include <glog/logging.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <iostream>

#include "src/buffer.h"
#include "src/buffer_output_producer.h"
#include "src/buffer_variables.h"
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

struct LineData {
  Range range;
  enum class Cursors { kNone, kInactive, kActive };
  Cursors cursors = Cursors::kNone;
};

class LineNumberOutputProducer : public OutputProducer {
 public:
  static size_t PrefixWidth(size_t lines_size) {
    return 1 + std::to_wstring(lines_size).size();
  }

  LineNumberOutputProducer(size_t lines_size,
                           std::function<LineData()> get_line_data)
      : width_(PrefixWidth(lines_size)),
        get_line_data_(std::move(get_line_data)),
        lines_size_(lines_size) {}

  void WriteLine(Options options) override {
    auto data = get_line_data_();
    if (data.range.begin.line >= lines_size_) {
      return;  // Happens when the buffer is smaller than the screen.
    }
    std::wstring number = std::to_wstring(data.range.begin.line);
    CHECK_LE(number.size(), width_ - 1);
    std::wstring padding(width_ - number.size() - 1, L' ');
    switch (data.cursors) {
      case LineData::Cursors::kNone:
        options.receiver->AddModifier(LineModifier::DIM);
        break;
      case LineData::Cursors::kInactive:
        options.receiver->AddModifier(LineModifier::BLUE);
        break;
      case LineData::Cursors::kActive:
        options.receiver->AddModifier(LineModifier::CYAN);
        options.receiver->AddModifier(LineModifier::BOLD);
        break;
    }
    options.receiver->AddString(padding + number + L':');
  }

  size_t width() const { return width_; }

 private:
  const size_t width_;
  const std::function<LineData()> get_line_data_;
  const size_t lines_size_;
};

std::unique_ptr<OutputProducer> BufferWidget::CreateOutputProducer() {
  auto buffer = Lock();
  if (buffer == nullptr) {
    return nullptr;
  }

  bool paste_mode = buffer->Read(buffer_variables::paste_mode());
  auto buffer_output_producer = std::make_unique<BufferOutputProducer>(
      buffer, lines_,
      columns_ - (paste_mode ? 0
                             : LineNumberOutputProducer::PrefixWidth(
                                   buffer->lines_size())),
      view_start_, zoomed_out_tree_);
  if (paste_mode) {
    return buffer_output_producer;
  }

  std::vector<VerticalSplitOutputProducer::Column> columns(2);

  auto line_numbers = std::make_unique<LineNumberOutputProducer>(
      buffer->lines_size(), [producer = buffer_output_producer.get()]() {
        LineData output;
        output.range = producer->GetCurrentRange();
        if (producer->HasActiveCursor()) {
          output.cursors = LineData::Cursors::kActive;
        } else if (!producer->GetCurrentCursors().empty()) {
          output.cursors = LineData::Cursors::kInactive;
        } else {
          output.cursors = LineData::Cursors::kNone;
        }
        return output;
      });
  columns[0].width = line_numbers->width();
  columns[0].producer = std::move(line_numbers);

  columns[1].producer = std::move(buffer_output_producer);

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
