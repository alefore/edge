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
#include "src/editor.h"
#include "src/horizontal_split_output_producer.h"
#include "src/line_number_output_producer.h"
#include "src/line_scroll_control.h"
#include "src/status_output_producer.h"
#include "src/vertical_split_output_producer.h"
#include "src/widget.h"
#include "src/wstring.h"

namespace afc {
namespace editor {
namespace {
ColumnNumber GetCurrentColumn(OpenBuffer* buffer) {
  if (buffer->lines_size() == LineNumberDelta(0)) {
    return ColumnNumber(0);
  } else if (buffer->position().line > buffer->EndLine()) {
    return buffer->contents()->back()->EndColumn();
  } else if (!buffer->IsLineFiltered(buffer->position().line)) {
    return ColumnNumber(0);
  } else {
    return min(buffer->position().column,
               buffer->LineAt(buffer->position().line)->EndColumn());
  }
}

ColumnNumber GetDesiredViewStartColumn(OpenBuffer* buffer) {
  if (buffer->Read(buffer_variables::wrap_long_lines)) {
    return ColumnNumber(0);
  }
  // TODO: This is bogus.
  ColumnNumberDelta effective_size = ColumnNumberDelta(80);
  effective_size -=
      min(effective_size,
          /* GetInitialPrefixSize(*buffer) */ ColumnNumberDelta(3ul));
  ColumnNumber column = GetCurrentColumn(buffer);
  return column - min(column.ToDelta(), effective_size);
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
const BufferWidget* BufferWidget::GetActiveLeaf() const { return this; }

class EmptyProducer : public OutputProducer {
  void WriteLine(Options) override {}
};

std::unique_ptr<OutputProducer> BufferWidget::CreateOutputProducer() {
  LOG(INFO) << "Buffer widget: CreateOutputProducer.";
  auto buffer = Lock();
  if (buffer == nullptr) {
    return std::make_unique<EmptyProducer>();
  }

  // We always show the buffer's status, even if the status::text is empty.
  auto status_lines = LineNumberDelta(1);

  bool paste_mode = buffer->Read(buffer_variables::paste_mode);

  auto line_scroll_control =
      LineScrollControl::New(line_scroll_control_options_);

  std::unique_ptr<OutputProducer> output =
      std::make_unique<BufferOutputProducer>(
          buffer, line_scroll_control->NewReader(), lines_ - status_lines,
          line_scroll_control_options_.columns_shown, view_start_.column,
          zoomed_out_tree_);
  if (!paste_mode) {
    std::vector<VerticalSplitOutputProducer::Column> columns(3);

    auto line_numbers = std::make_unique<LineNumberOutputProducer>(
        buffer, line_scroll_control->NewReader());

    columns[0].width = line_numbers->width();
    columns[0].producer = std::move(line_numbers);

    columns[1].width = line_scroll_control_options_.columns_shown;
    columns[1].producer = std::move(output);

    columns[2].producer = std::make_unique<BufferMetadataOutputProducer>(
        buffer, line_scroll_control->NewReader(), lines_ - status_lines,
        view_start_.line, zoomed_out_tree_);

    output =
        std::make_unique<VerticalSplitOutputProducer>(std::move(columns), 1);
  }

  if (status_lines > LineNumberDelta(0)) {
    std::vector<HorizontalSplitOutputProducer::Row> rows(2);
    rows[0].producer = std::move(output);
    rows[0].lines = lines_ - status_lines;

    rows[1].producer = std::make_unique<StatusOutputProducer>(
        buffer->status(), buffer.get(), buffer->editor()->modifiers());
    rows[1].lines = status_lines;

    output = std::make_unique<HorizontalSplitOutputProducer>(
        std::move(rows),
        buffer->status()->GetType() == Status::Type::kPrompt ? 1 : 0);
  }
  return output;
}

void BufferWidget::SetSize(LineNumberDelta lines, ColumnNumberDelta columns) {
  lines_ = lines;
  columns_ = columns;
  RecomputeData();
}

LineNumberDelta BufferWidget::lines() const { return lines_; }
ColumnNumberDelta BufferWidget::columns() const { return columns_; }

LineNumberDelta BufferWidget::MinimumLines() {
  auto buffer = Lock();
  return buffer == nullptr
             ? LineNumberDelta(0)
             : max(LineNumberDelta(0),
                   LineNumberDelta(buffer->Read(
                       buffer_variables::buffer_list_context_lines)));
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

  // TODO: If the buffer has multiple views of different sizes, we're gonna have
  // a bad time.
  auto status_lines = min(lines_, LineNumberDelta(1));
  auto buffer_lines = lines_ - status_lines;
  buffer->SetTerminalSize(buffer_lines, columns_);

  bool paste_mode = buffer->Read(buffer_variables::paste_mode);

  line_scroll_control_options_.buffer = buffer;
  line_scroll_control_options_.lines_shown = buffer_lines;
  line_scroll_control_options_.columns_shown =
      columns_ -
      (paste_mode
           ? ColumnNumberDelta(0)
           : LineNumberOutputProducer::PrefixWidth(buffer->lines_size()));
  if (!buffer->Read(buffer_variables::paste_mode)) {
    line_scroll_control_options_.columns_shown =
        min(line_scroll_control_options_.columns_shown,
            ColumnNumberDelta(buffer->Read(buffer_variables::line_width)));
  }

  LineNumber line = min(buffer->position().line, buffer->EndLine());
  LineNumberDelta margin_lines =
      buffer->Read(buffer_variables::pts)
          ? LineNumberDelta(0)
          : min(max(buffer_lines / 2 - LineNumberDelta(1), LineNumberDelta(0)),
                max(LineNumberDelta(ceil(
                        buffer->Read(buffer_variables::margin_lines_ratio) *
                        buffer_lines.line_delta)),
                    max(LineNumberDelta(
                            buffer->Read(buffer_variables::margin_lines)),
                        LineNumberDelta(0))));
  CHECK_GE(margin_lines, LineNumberDelta(0));

  if (view_start_.line > line - min(margin_lines, line.ToDelta()) &&
      (buffer->child_pid() != -1 || buffer->fd() == nullptr)) {
    view_start_.line = line - min(margin_lines, line.ToDelta());
  } else if (view_start_.line + buffer_lines <=
             min(LineNumber(0) + buffer->lines_size(), line + margin_lines)) {
    view_start_.line =
        min(LineNumber(0) + buffer->lines_size() - LineNumberDelta(1),
            line + margin_lines) -
        buffer_lines + LineNumberDelta(1);
  }

  view_start_.column = GetDesiredViewStartColumn(buffer.get());
  line_scroll_control_options_.begin = view_start_;
  line_scroll_control_options_.initial_column = view_start_.column;

  auto simplified_parse_tree = buffer->simplified_parse_tree();
  if (buffer_lines > LineNumberDelta(0) && simplified_parse_tree != nullptr &&
      simplified_parse_tree != simplified_parse_tree_) {
    zoomed_out_tree_ = std::make_shared<ParseTree>(ZoomOutTree(
        *buffer->simplified_parse_tree(), buffer->lines_size(), buffer_lines));
    simplified_parse_tree_ = simplified_parse_tree;
  }
}

}  // namespace editor
}  // namespace afc
