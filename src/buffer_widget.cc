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
#include "src/char_buffer.h"
#include "src/editor.h"
#include "src/horizontal_split_output_producer.h"
#include "src/line_number_output_producer.h"
#include "src/line_scroll_control.h"
#include "src/status_output_producer.h"
#include "src/vertical_split_output_producer.h"
#include "src/widget.h"
#include "src/wstring.h"

namespace afc::editor {
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

ColumnNumber GetDesiredViewStartColumn(OpenBuffer* buffer,
                                       ColumnNumberDelta effective_size) {
  if (buffer->Read(buffer_variables::wrap_long_lines)) {
    return ColumnNumber(0);
  }
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

void BufferWidget::ForEachBufferWidget(
    std::function<void(BufferWidget*)> callback) {
  callback(this);
}

void BufferWidget::ForEachBufferWidgetConst(
    std::function<void(const BufferWidget*)> callback) const {
  callback(this);
}

class SectionBracketsProducer : public OutputProducer {
 public:
  SectionBracketsProducer(LineNumberDelta lines) : lines_(lines) {}

  Generator Next() override {
    wstring c;
    if (current_line_ == LineNumber(0)) {
      c = L"╭";
    } else if ((current_line_ + LineNumberDelta(1)).ToDelta() == lines_) {
      c = L"╰";
    } else {
      c = L"│";
    }
    ++current_line_;
    return Generator{
        std::hash<wstring>{}(c), [c]() {
          return LineWithCursor{
              std::make_shared<Line>(Line::Options(NewLazyString(c))),
              std::nullopt};
        }};
  }

 private:
  const LineNumberDelta lines_;
  LineNumber current_line_;
};

std::unique_ptr<OutputProducer> LinesSpanView(
    std::shared_ptr<OpenBuffer> buffer,
    std::shared_ptr<LineScrollControl> line_scroll_control,
    LineColumnDelta output_size, size_t sections_count) {
  std::unique_ptr<OutputProducer> main_contents =
      std::make_unique<BufferOutputProducer>(
          buffer, line_scroll_control->NewReader(), output_size);

  if (buffer->Read(buffer_variables::paste_mode)) {
    return std::move(main_contents);
  }

  std::vector<VerticalSplitOutputProducer::Column> columns;

  auto line_numbers = std::make_unique<LineNumberOutputProducer>(
      buffer, line_scroll_control->NewReader());
  auto width = line_numbers->width();
  if (sections_count > 1) {
    columns.push_back(
        {std::make_unique<SectionBracketsProducer>(output_size.line),
         ColumnNumberDelta(1)});
  }

  columns.push_back({std::move(line_numbers), width});
  columns.push_back({std::move(main_contents), output_size.column});
  columns.push_back(
      {std::make_unique<BufferMetadataOutputProducer>(
           buffer, line_scroll_control->NewReader(), output_size.line,
           buffer->current_zoomed_out_parse_tree(output_size.line)),
       std::nullopt});
  return std::make_unique<VerticalSplitOutputProducer>(
      std::move(columns), sections_count > 1 ? 2 : 1);
}

std::set<Range> MergeSections(std::set<Range> input) {
  std::set<Range> output;
  for (auto& section : input) {
    auto merged_section =
        output.empty() ? std::nullopt : output.rbegin()->Union(section);
    if (merged_section.has_value()) {
      output.erase(--output.end());
    }
    output.insert(merged_section.value_or(section));
  }
  return output;
}

LineNumberDelta SumSectionsLines(const std::set<Range> sections) {
  LineNumberDelta output;
  for (auto& range : sections) {
    output += range.end.line - range.begin.line;
  }
  return output;
}

std::set<Range> ExpandSections(LineNumber end_line,
                               const std::set<Range> sections) {
  std::set<Range> output;
  static const auto kMargin = LineNumberDelta(1);
  for (const auto& section : sections) {
    output.insert(
        Range(LineColumn(section.begin.line.MinusHandlingOverflow(kMargin)),
              LineColumn(min(end_line + LineNumberDelta(1),
                             section.end.line + kMargin))));
  }
  return output;
}

std::unique_ptr<OutputProducer> ViewMultipleCursors(
    std::shared_ptr<OpenBuffer> buffer, LineColumnDelta output_size,
    const LineScrollControl::Options line_scroll_control_options) {
  std::set<Range> sections;
  for (auto& cursor : *buffer->active_cursors()) {
    sections.insert(Range(
        LineColumn(cursor.line),
        LineColumn(min(buffer->EndLine(), cursor.line + LineNumberDelta(1)))));
  }
  bool first_run = true;
  while (first_run || SumSectionsLines(sections) <
                          min(output_size.line, buffer->contents()->size())) {
    VLOG(4) << "Expanding " << sections.size()
            << " with size: " << SumSectionsLines(sections);
    sections =
        MergeSections(ExpandSections(buffer->EndLine(), std::move(sections)));
    first_run = false;
  }

  std::vector<HorizontalSplitOutputProducer::Row> rows;
  size_t active_index = 0;
  size_t index = 0;
  for (const auto& section : sections) {
    LineScrollControl::Options options = line_scroll_control_options;
    options.lines_shown = section.end.line - section.begin.line;
    // TODO: Maybe take columns into account? Ugh.
    options.begin = LineColumn(section.begin.line);
    rows.push_back(
        {LinesSpanView(buffer, LineScrollControl::New(options),
                       LineColumnDelta(options.lines_shown, output_size.column),
                       sections.size()),
         options.lines_shown});

    if (section.Contains(buffer->position())) {
      active_index = index;
    }
    index++;
  }
  return std::make_unique<HorizontalSplitOutputProducer>(std::move(rows),
                                                         active_index);
}

std::unique_ptr<OutputProducer> BufferWidget::CreateOutputProducer(
    OutputProducerOptions options) const {
  auto buffer = leaf_.lock();
  if (buffer == nullptr) {
    return OutputProducer::Empty();
  }

  LineScrollControl::Options line_scroll_control_options;
  LOG(INFO) << "BufferWidget::RecomputeData: "
            << buffer->Read(buffer_variables::name);

  auto status_lines = min(options.size.line, LineNumberDelta(1));
  // Screen lines that are dedicated to the buffer.
  auto buffer_lines = options.size.line - status_lines;

  LineColumnDelta buffer_view_size(buffer_lines, options.size.column);
  if (!buffer_view_size_registration_.has_value() ||
      buffer_view_size_registration_.value() != buffer_view_size) {
    buffer_view_size_registration_ = buffer_view_size;
    buffer->viewers()->set_view_size(buffer_view_size);
  }

  bool paste_mode = buffer->Read(buffer_variables::paste_mode);

  line_scroll_control_options.buffer = buffer;
  line_scroll_control_options.lines_shown = buffer_lines;
  line_scroll_control_options.columns_shown =
      options.size.column -
      (paste_mode
           ? ColumnNumberDelta(0)
           : LineNumberOutputProducer::PrefixWidth(buffer->lines_size()));
  if (!buffer->Read(buffer_variables::paste_mode)) {
    line_scroll_control_options.columns_shown =
        min(line_scroll_control_options.columns_shown,
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

  if (view_start_.line + min(margin_lines, line.ToDelta()) > line &&
      (buffer->child_pid() != -1 || buffer->fd() == nullptr)) {
    view_start_.line = line - min(margin_lines, line.ToDelta());
  } else if (view_start_.line + buffer_lines <=
             min(LineNumber(0) + buffer->lines_size(), line + margin_lines)) {
    CHECK_GT(buffer->lines_size(), LineNumberDelta(0));
    auto view_end_line =
        min(LineNumber(0) + buffer->lines_size() - LineNumberDelta(1),
            line + margin_lines);
    view_start_.line =
        view_end_line + LineNumberDelta(1) -
        min(buffer_lines, view_end_line.ToDelta() + LineNumberDelta(1));
  }

  view_start_.column =
      GetDesiredViewStartColumn(buffer.get(), options.size.column);
  line_scroll_control_options.begin = view_start_;
  line_scroll_control_options.initial_column = view_start_.column;

  if (!buffer->Read(buffer_variables::multiple_cursors)) {
    auto scroll_reader =
        LineScrollControl::New(line_scroll_control_options)->NewReader();
    std::vector<Range> positions;
    while ((positions.empty() || positions.back().end <= buffer->position()) &&
           scroll_reader->GetRange().has_value()) {
      positions.push_back(scroll_reader->GetRange().value());
      scroll_reader->RangeDone();
    }

    LineNumber capped_line =
        std::min(buffer->position().line, LineNumber(0) + buffer->lines_size());
    LineNumberDelta lines_remaining =
        buffer->lines_size() - capped_line.ToDelta();

    LineNumberDelta effective_bottom_margin_lines =
        std::min(buffer_lines, std::min(margin_lines, lines_remaining));
    if (LineNumber(positions.size()) + effective_bottom_margin_lines >
        LineNumber(0) + buffer_lines) {
      // No need to adjust line_scroll_control_options.initial_column, since
      // that controls where continuation lines begin.
      view_start_ =
          positions[positions.size() -
                    (buffer_lines - effective_bottom_margin_lines).line_delta -
                    1]
              .begin;
      line_scroll_control_options.begin = view_start_;
    }
  }

  std::unique_ptr<OutputProducer> output;

  LineColumnDelta buffer_output_size(options.size.line - status_lines,
                                     line_scroll_control_options.columns_shown);

  if (buffer->Read(buffer_variables::multiple_cursors)) {
    output = ViewMultipleCursors(buffer, buffer_output_size,
                                 line_scroll_control_options);
  } else {
    output = LinesSpanView(buffer,
                           LineScrollControl::New(line_scroll_control_options),
                           buffer_output_size, 1);
  }

  if (status_lines > LineNumberDelta(0)) {
    std::vector<HorizontalSplitOutputProducer::Row> rows(2);
    rows[0].producer = std::move(output);
    rows[0].lines = options.size.line - status_lines;

    rows[1].producer = std::make_unique<StatusOutputProducer>(
        buffer->status(), buffer.get(), buffer->editor()->modifiers());
    rows[1].lines = status_lines;

    output = std::make_unique<HorizontalSplitOutputProducer>(
        std::move(rows),
        buffer->status()->GetType() == Status::Type::kPrompt ? 1 : 0);
  }
  return output;
}

LineNumberDelta BufferWidget::MinimumLines() const {
  auto buffer = Lock();
  return buffer == nullptr
             ? LineNumberDelta(0)
             : max(LineNumberDelta(0),
                   LineNumberDelta(buffer->Read(
                       buffer_variables::buffer_list_context_lines)));
}

void BufferWidget::RemoveBuffer(OpenBuffer* buffer) {
  if (Lock().get() == buffer) {
    SetBuffer(std::shared_ptr<OpenBuffer>());
  }
}

size_t BufferWidget::CountLeaves() const { return 1; }

int BufferWidget::AdvanceActiveLeafWithoutWrapping(int delta) { return delta; }

void BufferWidget::SetActiveLeavesAtStart() {}

LineColumn BufferWidget::view_start() const { return view_start_; }

std::shared_ptr<OpenBuffer> BufferWidget::Lock() const { return leaf_.lock(); }

void BufferWidget::SetBuffer(std::weak_ptr<OpenBuffer> buffer) {
  leaf_ = std::move(buffer);
  buffer_view_size_registration_ = std::nullopt;
}

}  // namespace afc::editor
