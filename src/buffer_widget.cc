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

ColumnNumber GetCurrentColumn(OpenBuffer* buffer) {
  if (buffer->lines_size() == LineNumberDelta(0)) {
    return ColumnNumber(0);
  } else if (buffer->position().line > buffer->EndLine()) {
    return buffer->contents()->back()->EndColumn();
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
  // Screen lines that are dedicated to the buffer.
  auto buffer_lines = size.line - status_lines;

  auto buffer_view_size = LineColumnDelta(buffer_lines, size.column);
  buffer->viewers()->set_view_size(buffer_view_size);

  bool paste_mode = buffer->Read(buffer_variables::paste_mode);

  LineScrollControl::Options line_scroll_control_options;
  line_scroll_control_options.buffer = buffer;
  line_scroll_control_options.lines_shown = buffer_lines;
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

  if (output.view_start.line + min(margin_lines, line.ToDelta()) > line &&
      (buffer->child_pid() != -1 || buffer->fd() == nullptr)) {
    output.view_start.line = line - min(margin_lines, line.ToDelta());
  } else if (output.view_start.line + buffer_lines <=
             min(LineNumber(0) + buffer->lines_size(), line + margin_lines)) {
    CHECK_GT(buffer->lines_size(), LineNumberDelta(0));
    auto view_end_line =
        min(LineNumber(0) + buffer->lines_size() - LineNumberDelta(1),
            line + margin_lines);
    output.view_start.line =
        view_end_line + LineNumberDelta(1) -
        min(buffer_lines, view_end_line.ToDelta() + LineNumberDelta(1));
  }

  output.view_start.column =
      GetDesiredViewStartColumn(buffer.get(), size.column);
  line_scroll_control_options.begin = output.view_start;
  line_scroll_control_options.initial_column = output.view_start.column;

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
      output.view_start =
          positions[positions.size() -
                    (buffer_lines - effective_bottom_margin_lines).line_delta -
                    1]
              .begin;
      line_scroll_control_options.begin = output.view_start;
    }
  }

  input.output_producer_options.size =
      LineColumnDelta(buffer_lines, line_scroll_control_options.columns_shown);

  if (buffer->Read(buffer_variables::multiple_cursors)) {
    output.producer = ViewMultipleCursors(buffer, input.output_producer_options,
                                          line_scroll_control_options);
  } else {
    output.producer = LinesSpanView(
        buffer, LineScrollControl::New(line_scroll_control_options),
        input.output_producer_options, 1);
  }

  if (status_lines > LineNumberDelta(0)) {
    std::vector<HorizontalSplitOutputProducer::Row> rows(2);
    rows[0].producer = std::move(output.producer);
    rows[0].lines = buffer_lines;

    rows[1].producer = status_output_producer_supplier.CreateOutputProducer(
        LineColumnDelta(status_lines, size.column));
    rows[1].lines = status_lines;

    output.producer = std::make_unique<HorizontalSplitOutputProducer>(
        std::move(rows),
        buffer->status()->GetType() == Status::Type::kPrompt ? 1 : 0);
  }
  return output;
}

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

std::unique_ptr<OutputProducer> BufferWidget::CreateOutputProducer(
    OutputProducerOptions options) const {
  auto buffer = leaf_.lock();
  BufferOutputProducerInput input;
  input.output_producer_options = std::move(options);
  input.buffer = buffer;
  input.view_start = view_start();
  auto output = CreateBufferOutputProducer(std::move(input));
  // We avoid updating the desired view_start while the buffer is still being
  // read.
  if (buffer != nullptr &&
      buffer->lines_size() >= buffer->position().line.ToDelta() &&
      (buffer->child_pid() != -1 || buffer->fd() == nullptr)) {
    buffer->Set(buffer_variables::view_start, output.view_start);
  }

  if (options.position_in_parent.has_value()) {
    std::vector<HorizontalSplitOutputProducer::Row> nested_rows;
    FrameOutputProducer::Options frame_options;
    frame_options.title = Name();
    frame_options.position_in_parent = options.position_in_parent.value();
    bool is_active = options.is_active;
    if (is_active && options.main_cursor_behavior ==
                         OutputProducerOptions::MainCursorBehavior::kIgnore) {
      frame_options.active_state =
          FrameOutputProducer::Options::ActiveState::kActive;
    }

    static const auto kFrameLines = LineNumberDelta(1);

    bool add_left_frame = true;
    if (buffer != nullptr) {
      frame_options.extra_information =
          OpenBuffer::FlagsToString(buffer->Flags());
      frame_options.width =
          ColumnNumberDelta(buffer->Read(buffer_variables::line_width));
      add_left_frame = !buffer->Read(buffer_variables::paste_mode);
    }

    frame_options.prefix =
        (options.size.line > kFrameLines && add_left_frame) ? L"╭" : L"─";

    nested_rows.push_back(
        {std::make_unique<FrameOutputProducer>(std::move(frame_options)),
         LineNumberDelta(1)});

    options.size.line -= nested_rows.back().lines;
    options.main_cursor_behavior =
        options.is_active
            ? options.main_cursor_behavior
            : Widget::OutputProducerOptions::MainCursorBehavior::kHighlight;

    if (add_left_frame) {
      output.producer = AddLeftFrame(
          std::move(output.producer), options.size.line,
          is_active ? LineModifierSet{LineModifier::BOLD, LineModifier::CYAN}
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
             : max(LineNumberDelta(0),
                   LineNumberDelta(buffer->Read(
                       buffer_variables::buffer_list_context_lines)));
}

LineColumn BufferWidget::view_start() const {
  auto buffer = leaf_.lock();
  return buffer == nullptr ? LineColumn()
                           : buffer->Read(buffer_variables::view_start);
}

std::shared_ptr<OpenBuffer> BufferWidget::Lock() const { return leaf_.lock(); }

void BufferWidget::SetBuffer(std::weak_ptr<OpenBuffer> buffer) {
  leaf_ = std::move(buffer);
}

}  // namespace afc::editor
