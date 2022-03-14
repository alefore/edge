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
#include "src/frame_output_producer.h"
#include "src/horizontal_split_output_producer.h"
#include "src/line_number_output_producer.h"
#include "src/line_scroll_control.h"
#include "src/section_brackets_producer.h"
#include "src/status_output_producer.h"
#include "src/tests/tests.h"
#include "src/vertical_split_output_producer.h"
#include "src/widget.h"
#include "src/wstring.h"

namespace afc::editor {
namespace {
static const auto kTopFrameLines = LineNumberDelta(1);
static const auto kStatusFrameLines = LineNumberDelta(1);

std::unique_ptr<OutputProducer> ProducerForString(std::wstring src,
                                                  LineModifierSet modifiers) {
  Line::Options options;
  options.AppendString(std::move(src), std::move(modifiers));
  return OutputProducer::Constant(
      {.line = std::make_shared<Line>(std::move(options))});
}

std::unique_ptr<OutputProducer> AddLeftFrame(
    std::unique_ptr<OutputProducer> producer, LineNumberDelta lines,
    LineModifierSet modifiers) {
  if (lines.IsZero()) {
    return OutputProducer::Empty();
  }

  std::vector<VerticalSplitOutputProducer::Column> columns;

  std::vector<HorizontalSplitOutputProducer::Row> rows;
  if (lines > LineNumberDelta(1)) {
    rows.push_back({
        .producer = ProducerForString(L"│", modifiers),
        .lines = lines - LineNumberDelta(1),
    });
  }
  rows.push_back({.producer = ProducerForString(L"╰", modifiers),
                  .lines = LineNumberDelta(1)});

  columns.push_back(
      {.producer =
           std::make_unique<HorizontalSplitOutputProducer>(std::move(rows), 0),
       .width = ColumnNumberDelta(1)});

  columns.push_back({.producer = std::move(producer)});

  return std::make_unique<VerticalSplitOutputProducer>(std::move(columns), 1);
}

std::unique_ptr<OutputProducer> LinesSpanView(
    std::shared_ptr<OpenBuffer> buffer,
    std::shared_ptr<LineScrollControl> line_scroll_control,
    Widget::OutputProducerOptions output_producer_options,
    size_t sections_count) {
  std::unique_ptr<OutputProducer> main_contents =
      std::make_unique<BufferOutputProducer>(
          buffer, line_scroll_control->NewReader(), output_producer_options);

  if (buffer->Read(buffer_variables::paste_mode)) {
    return main_contents;
  }

  std::vector<VerticalSplitOutputProducer::Column> columns;

  auto line_numbers = std::make_unique<LineNumberOutputProducer>(
      buffer, line_scroll_control->NewReader());
  auto width = line_numbers->width();
  if (sections_count > 1) {
    columns.push_back({std::make_unique<SectionBracketsProducer>(
                           output_producer_options.size.line),
                       ColumnNumberDelta(1)});
  }

  columns.push_back({std::move(line_numbers), width});
  columns.push_back(
      {std::move(main_contents), output_producer_options.size.column});
  columns.push_back({std::make_unique<BufferMetadataOutputProducer>(
                         buffer, line_scroll_control->NewReader(),
                         output_producer_options.size.line,
                         buffer->current_zoomed_out_parse_tree(
                             output_producer_options.size.line)),
                     std::nullopt});
  return std::make_unique<VerticalSplitOutputProducer>(
      std::move(columns), sections_count > 1 ? 2 : 1);
}

std::set<Range> MergeSections(std::set<Range> input) {
  std::set<Range> output;
  for (auto& section : input) {
    if (!output.empty()) {
      if (auto result = output.rbegin()->Union(section); !result.IsError()) {
        output.erase(--output.end());
        output.insert(result.value());
        continue;
      }
    }
    output.insert(section);
  }
  return output;
}

const bool merge_sections_tests_registration = tests::Register(
    L"MergeSectionsTests",
    {{.name = L"Empty",
      .callback = [] { CHECK_EQ(MergeSections({}).size(), 0ul); }},
     {.name = L"Singleton",
      .callback =
          [] {
            Range input = Range(LineColumn(LineNumber(10), ColumnNumber(0)),
                                LineColumn(LineNumber(15), ColumnNumber(0)));
            auto output = MergeSections({input});
            CHECK_EQ(output.size(), 1ul);
            CHECK_EQ(*output.begin(), input);
          }},
     {.name = L"Disjoint",
      .callback =
          [] {
            Range input_0 = Range(LineColumn(LineNumber(10), ColumnNumber(0)),
                                  LineColumn(LineNumber(15), ColumnNumber(0)));
            Range input_1 = Range(LineColumn(LineNumber(30), ColumnNumber(0)),
                                  LineColumn(LineNumber(35), ColumnNumber(0)));
            Range input_2 = Range(LineColumn(LineNumber(50), ColumnNumber(0)),
                                  LineColumn(LineNumber(55), ColumnNumber(0)));
            auto output = MergeSections({input_0, input_1, input_2});
            CHECK_EQ(output.size(), 3ul);
            CHECK_EQ(output.count(input_0), 1ul);
            CHECK_EQ(output.count(input_1), 1ul);
            CHECK_EQ(output.count(input_2), 1ul);
          }},
     {
         .name = L"SomeOverlap",
         .callback =
             [] {
               Range input_0 =
                   Range(LineColumn(LineNumber(10), ColumnNumber(0)),
                         LineColumn(LineNumber(15), ColumnNumber(0)));
               Range input_1 =
                   Range(LineColumn(LineNumber(13), ColumnNumber(0)),
                         LineColumn(LineNumber(18), ColumnNumber(0)));
               Range input_separate =
                   Range(LineColumn(LineNumber(50), ColumnNumber(0)),
                         LineColumn(LineNumber(55), ColumnNumber(0)));
               auto output = MergeSections({input_0, input_1, input_separate});
               CHECK_EQ(output.size(), 2ul);
               CHECK_EQ(output.count(
                            Range(LineColumn(LineNumber(10), ColumnNumber(0)),
                                  LineColumn(LineNumber(18), ColumnNumber(0)))),
                        1ul);
               CHECK_EQ(output.count(input_separate), 1ul);
             },
     }});

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
    std::shared_ptr<OpenBuffer> buffer,
    Widget::OutputProducerOptions output_producer_options,
    const LineScrollControl::Options line_scroll_control_options) {
  std::set<Range> sections;
  for (auto& cursor : *buffer->active_cursors()) {
    sections.insert(Range(
        LineColumn(cursor.line),
        LineColumn(min(buffer->EndLine(), cursor.line + LineNumberDelta(1)))));
  }
  bool first_run = true;
  while (first_run ||
         SumSectionsLines(sections) < min(output_producer_options.size.line,
                                          buffer->contents()->size())) {
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
    Widget::OutputProducerOptions section_output_producer_options =
        output_producer_options;
    section_output_producer_options.size = LineColumnDelta(
        options.lines_shown, output_producer_options.size.column);
    rows.push_back(
        {LinesSpanView(buffer, LineScrollControl::New(options),
                       section_output_producer_options, sections.size()),
         options.lines_shown});

    if (section.Contains(buffer->position())) {
      active_index = index;
    }
    index++;
  }
  return std::make_unique<HorizontalSplitOutputProducer>(std::move(rows),
                                                         active_index);
}

struct BufferRenderPlan {
  // Contains one entry for each line to render.
  std::vector<Range> lines;

  // If present, an index in `lines` corresponding to the position with the main
  // cursor.
  std::optional<size_t> cursor_index;

  enum class StatusPosition { kTop, kBottom };
  StatusPosition status_position = StatusPosition::kBottom;
};

BufferRenderPlan GetBufferRenderPlan(
    LineScrollControl::Options line_scroll_control_options, LineColumn position,
    LineNumberDelta status_lines) {
  BufferRenderPlan output;

  // Initialize output.lines:
  for (auto scroll_reader =
           LineScrollControl::New(line_scroll_control_options)->NewReader();
       LineNumberDelta(output.lines.size()) <
           line_scroll_control_options.lines_shown &&
       scroll_reader->GetRange().has_value();
       scroll_reader->RangeDone()) {
    output.lines.push_back(scroll_reader->GetRange().value());
    if (scroll_reader->HasActiveCursor() && !output.cursor_index.has_value())
      output.cursor_index = output.lines.size();
  }

  // Initialize output.status_position:
  if (output.cursor_index.value_or(0) > 3 * output.lines.size() / 5)
    output.status_position = BufferRenderPlan::StatusPosition::kTop;
  switch (output.status_position) {
    case BufferRenderPlan::StatusPosition::kBottom:
      output.lines.resize(std::max(
          0,
          (line_scroll_control_options.lines_shown - status_lines).line_delta));
      break;
    case BufferRenderPlan::StatusPosition::kTop:
      break;
  }
  return output;
}

}  // namespace

BufferOutputProducerOutput CreateBufferOutputProducer(
    BufferOutputProducerInput input) {
  BufferOutputProducerOutput output;
  output.view_start = input.view_start;

  auto buffer = input.buffer;
  if (buffer == nullptr) {
    output.producer = OutputProducer::Empty();
    return output;
  }

  auto size = input.output_producer_options.size;
  LOG(INFO) << "BufferWidget::RecomputeData: "
            << buffer->Read(buffer_variables::name);

  StatusOutputProducerSupplier status_output_producer_supplier(
      buffer->status(), buffer.get(), buffer->editor()->modifiers());
  const auto status_lines =
      min(size.line / 4, status_output_producer_supplier.lines());

  // auto buffer_lines = size.line - status_lines;

  auto buffer_view_size = LineColumnDelta(size.line, size.column);
  buffer->viewers()->set_view_size(buffer_view_size);

  bool paste_mode = buffer->Read(buffer_variables::paste_mode);

  LineScrollControl::Options line_scroll_control_options;
  line_scroll_control_options.buffer = buffer;
  line_scroll_control_options.lines_shown = size.line;
  line_scroll_control_options.columns_shown =
      size.column -
      (paste_mode
           ? ColumnNumberDelta(0)
           : LineNumberOutputProducer::PrefixWidth(buffer->lines_size()));
  if (auto w = ColumnNumberDelta(buffer->Read(buffer_variables::line_width));
      !buffer->Read(buffer_variables::paste_mode) && w > ColumnNumberDelta(1)) {
    line_scroll_control_options.columns_shown =
        min(line_scroll_control_options.columns_shown, w);
  }

  LineNumberDelta margin_lines =
      buffer->Read(buffer_variables::pts)
          ? LineNumberDelta(0)
          : min(max(size.line / 2 - LineNumberDelta(1), LineNumberDelta(0)),
                max(LineNumberDelta(ceil(
                        buffer->Read(buffer_variables::margin_lines_ratio) *
                        size.line.line_delta)),
                    max(LineNumberDelta(
                            buffer->Read(buffer_variables::margin_lines)),
                        LineNumberDelta(0))));
  CHECK_GE(margin_lines, LineNumberDelta(0));
  if (buffer->child_pid() != -1 || buffer->fd() == nullptr) {
    line_scroll_control_options.margin_lines = margin_lines;
  }

  line_scroll_control_options.begin = output.view_start;

  BufferRenderPlan plan = GetBufferRenderPlan(line_scroll_control_options,
                                              buffer->position(), status_lines);
  output.view_start = plan.lines[0].begin;
  input.output_producer_options.size =
      LineColumnDelta(LineNumberDelta(plan.lines.size()),
                      line_scroll_control_options.columns_shown);

  if (buffer->Read(buffer_variables::multiple_cursors)) {
    output.producer = ViewMultipleCursors(buffer, input.output_producer_options,
                                          line_scroll_control_options);
  } else {
    output.producer = LinesSpanView(
        buffer, LineScrollControl::New(line_scroll_control_options),
        input.output_producer_options, 1);
  }

  if (status_lines > LineNumberDelta(0)) {
    using HP = HorizontalSplitOutputProducer;
    HP::Row buffer_row = {.producer = std::move(output.producer),
                          .lines = LineNumberDelta(plan.lines.size())};
    HP::Row status_row = {
        .producer = status_output_producer_supplier.CreateOutputProducer(
            LineColumnDelta(status_lines, size.column)),
        .lines = status_lines,
        .overlap_behavior = HP::Row::OverlapBehavior::kFloat};

    size_t buffer_index = 0;
    size_t status_index = 1;
    switch (plan.status_position) {
      case BufferRenderPlan::StatusPosition::kTop:
        status_index = 0;
        buffer_index = 1;
        break;
      case BufferRenderPlan::StatusPosition::kBottom:
        buffer_index = 0;
        status_index = 1;
        break;
    }

    std::vector<HP::Row> rows(2);
    rows[buffer_index] = std::move(buffer_row);
    rows[status_index] = std::move(status_row);

    output.producer = std::make_unique<HP>(
        std::move(rows), buffer->status()->GetType() == Status::Type::kPrompt
                             ? status_index
                             : buffer_index);
  }
  return output;
}

BufferWidget::BufferWidget(Options options) : options_(std::move(options)) {}

std::unique_ptr<OutputProducer> BufferWidget::CreateOutputProducer(
    OutputProducerOptions options) const {
  auto buffer = options_.buffer.lock();
  BufferOutputProducerInput input;
  input.output_producer_options = std::move(options);
  input.buffer = buffer;
  input.view_start = view_start();
  if (options_.position_in_parent.has_value()) {
    input.output_producer_options.size.line =
        max(LineNumberDelta(),
            input.output_producer_options.size.line - kTopFrameLines);
  }
  auto output = CreateBufferOutputProducer(std::move(input));
  // We avoid updating the desired view_start while the buffer is still being
  // read.
  if (buffer != nullptr &&
      buffer->lines_size() >= buffer->position().line.ToDelta() &&
      (buffer->child_pid() != -1 || buffer->fd() == nullptr)) {
    buffer->Set(buffer_variables::view_start, output.view_start);
  }

  if (options_.position_in_parent.has_value()) {
    std::vector<HorizontalSplitOutputProducer::Row> nested_rows;
    FrameOutputProducer::Options frame_options;
    frame_options.title =
        buffer == nullptr ? L"" : buffer->Read(buffer_variables::name);

    frame_options.position_in_parent = options_.position_in_parent.value();
    if (options_.is_active &&
        options.main_cursor_behavior ==
            OutputProducerOptions::MainCursorBehavior::kIgnore) {
      frame_options.active_state =
          FrameOutputProducer::Options::ActiveState::kActive;
    }

    bool add_left_frame = true;
    if (buffer != nullptr) {
      frame_options.extra_information =
          OpenBuffer::FlagsToString(buffer->Flags());
      frame_options.width =
          ColumnNumberDelta(buffer->Read(buffer_variables::line_width));
      add_left_frame = !buffer->Read(buffer_variables::paste_mode);
    }

    frame_options.prefix =
        (options.size.line > kTopFrameLines && add_left_frame) ? L"╭" : L"─";

    nested_rows.push_back(
        {std::make_unique<FrameOutputProducer>(std::move(frame_options)),
         LineNumberDelta(1)});

    options.size.line -= nested_rows.back().lines;
    options.main_cursor_behavior =
        options_.is_active
            ? options.main_cursor_behavior
            : Widget::OutputProducerOptions::MainCursorBehavior::kHighlight;

    if (add_left_frame) {
      output.producer = AddLeftFrame(
          std::move(output.producer), options.size.line,
          options_.is_active
              ? LineModifierSet{LineModifier::BOLD, LineModifier::CYAN}
              : LineModifierSet{LineModifier::DIM});
    }
    nested_rows.push_back(
        {.producer = std::move(output.producer), .lines = options.size.line});
    output.producer = std::make_unique<HorizontalSplitOutputProducer>(
        std::move(nested_rows), 1);
  }
  return std::move(output.producer);
}

LineNumberDelta BufferWidget::MinimumLines() const {
  auto buffer = Lock();
  return buffer == nullptr
             ? LineNumberDelta(0)
             : (options_.position_in_parent.has_value() ? kTopFrameLines
                                                        : LineNumberDelta(0)) +
                   max(LineNumberDelta(0),
                       min(buffer->lines_size(),
                           LineNumberDelta(buffer->Read(
                               buffer_variables::buffer_list_context_lines)))) +
                   kStatusFrameLines;
}

LineNumberDelta BufferWidget::DesiredLines() const {
  auto buffer = Lock();
  return buffer == nullptr
             ? LineNumberDelta(0)
             : (options_.position_in_parent.has_value() ? kTopFrameLines
                                                        : LineNumberDelta(0)) +
                   buffer->lines_size() + kStatusFrameLines;
}

LineColumn BufferWidget::view_start() const {
  auto buffer = Lock();
  return buffer == nullptr ? LineColumn()
                           : buffer->Read(buffer_variables::view_start);
}

std::shared_ptr<OpenBuffer> BufferWidget::Lock() const {
  return options_.buffer.lock();
}

void BufferWidget::SetBuffer(std::weak_ptr<OpenBuffer> buffer) {
  options_.buffer = std::move(buffer);
}

}  // namespace afc::editor
