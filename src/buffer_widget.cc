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
#include "src/line_number_output_producer.h"
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
  if (buffer->Read(buffer_variables::wrap_long_lines)) {
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
  return buffer == nullptr ? L"" : buffer->Read(buffer_variables::name);
}

wstring BufferWidget::ToString() const {
  auto buffer = leaf_.lock();
  return L"[buffer tree leaf" +
         (buffer == nullptr ? L"nullptr"
                            : buffer->Read(buffer_variables::name)) +
         L"]";
}

BufferWidget* BufferWidget::GetActiveLeaf() { return this; }

class EmptyProducer : public OutputProducer {
  void WriteLine(Options) override {}
};

std::unique_ptr<OutputProducer> BufferWidget::CreateOutputProducer() {
  LOG(INFO) << "Buffer widget: CreateOutputProducer.";
  auto buffer = Lock();
  if (buffer == nullptr) {
    return std::make_unique<EmptyProducer>();
  }

  bool paste_mode = buffer->Read(buffer_variables::paste_mode);

  auto line_scroll_control =
      LineScrollControl::New(line_scroll_control_options_);

  auto buffer_output_producer = std::make_unique<BufferOutputProducer>(
      buffer, line_scroll_control->NewReader(), lines_,
      line_scroll_control_options_.columns_shown, view_start_.column,
      zoomed_out_tree_);
  if (paste_mode) {
    return buffer_output_producer;
  }

  std::vector<VerticalSplitOutputProducer::Column> columns(3);

  auto line_numbers = std::make_unique<LineNumberOutputProducer>(
      buffer, line_scroll_control->NewReader());

  columns[0].width = line_numbers->width();
  columns[0].producer = std::move(line_numbers);

  columns[1].width = line_scroll_control_options_.columns_shown;
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
                   buffer->Read(buffer_variables::buffer_list_context_lines));
}

void BufferWidget::RemoveBuffer(OpenBuffer* buffer) {
  if (Lock().get() == buffer) {
    leaf_ = std::shared_ptr<OpenBuffer>();
  }
}

size_t BufferWidget::CountLeaves() const { return 1; }

int BufferWidget::AdvanceActiveLeafWithoutWrapping(int delta) { return delta; }

void BufferWidget::SetActiveLeavesAtStart() {}

LineColumn BufferWidget::view_start() const { return view_start_; }

std::shared_ptr<OpenBuffer> BufferWidget::Lock() const { return leaf_.lock(); }

void BufferWidget::SetBuffer(std::weak_ptr<OpenBuffer> buffer) {
  leaf_ = std::move(buffer);
  RecomputeData();
}

void BufferWidget::RecomputeData() {
  line_scroll_control_options_ = LineScrollControl::Options();

  auto buffer = leaf_.lock();
  if (buffer == nullptr) {
    zoomed_out_tree_ = nullptr;
    return;
  }

  bool paste_mode = buffer->Read(buffer_variables::paste_mode);

  line_scroll_control_options_.buffer = buffer;
  line_scroll_control_options_.lines_shown = lines_;
  line_scroll_control_options_.columns_shown =
      columns_ -
      (paste_mode
           ? 0
           : LineNumberOutputProducer::PrefixWidth(buffer->lines_size()));
  if (!buffer->Read(buffer_variables::paste_mode)) {
    line_scroll_control_options_.columns_shown =
        min(line_scroll_control_options_.columns_shown,
            static_cast<size_t>(buffer->Read(buffer_variables::line_width)));
  }

  size_t line = min(buffer->position().line, buffer->contents()->size() - 1);
  size_t margin_lines =
      buffer->Read(buffer_variables::pts)
          ? 0
          : min(lines_ / 2 - 1,
                max(static_cast<size_t>(ceil(
                        buffer->Read(buffer_variables::margin_lines_ratio) *
                        lines_)),
                    static_cast<size_t>(
                        max(buffer->Read(buffer_variables::margin_lines), 0))));

  if (view_start_.line > line - min(margin_lines, line) &&
      (buffer->child_pid() != -1 || buffer->fd() == -1)) {
    view_start_.line = line - min(margin_lines, line);
  } else if (view_start_.line + lines_ <=
             min(buffer->lines_size(), line + margin_lines)) {
    view_start_.line =
        min(buffer->lines_size() - 1, line + margin_lines) - lines_ + 1;
  }

  view_start_.column = GetDesiredViewStartColumn(buffer.get());
  line_scroll_control_options_.begin = view_start_;
  line_scroll_control_options_.initial_column = view_start_.column;

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
