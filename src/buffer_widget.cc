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
#include "src/horizontal_center_output_producer.h"
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

LineWithCursor ProducerForString(std::wstring src, LineModifierSet modifiers) {
  Line::Options options;
  options.AppendString(std::move(src), std::move(modifiers));
  return LineWithCursor(Line(std::move(options)));
}

LineWithCursor::Generator::Vector AddLeftFrame(
    LineWithCursor::Generator::Vector lines, LineNumberDelta times,
    LineModifierSet modifiers) {
  if (times.IsZero()) {
    return RepeatLine(LineWithCursor(Line()), times);
  }

  ColumnsVector columns_vector{.index_active = 1, .lines = times};

  RowsVector rows_vector{.lines = times};
  if (times > LineNumberDelta(1)) {
    rows_vector.push_back(
        {.lines_vector = RepeatLine(ProducerForString(L"│", modifiers),
                                    times - LineNumberDelta(1))});
  }
  rows_vector.push_back(
      {.lines_vector =
           RepeatLine(ProducerForString(L"╰", modifiers), LineNumberDelta(1))});

  columns_vector.push_back(
      {.lines = OutputFromRowsVector(std::move(rows_vector)),
       .width = ColumnNumberDelta(1)});

  columns_vector.push_back({.lines = lines});

  return OutputFromColumnsVector(std::move(columns_vector));
}

LineWithCursor::Generator::Vector CenterVertically(
    LineWithCursor::Generator::Vector input, LineNumberDelta lines) {
  if (input.size() >= lines) return input;
  std::vector<LineWithCursor::Generator> prefix(
      ((lines - input.size()) / 2).line_delta,
      LineWithCursor::Generator::Empty());
  input.lines.insert(input.lines.begin(), prefix.begin(), prefix.end());
  input.lines.resize(lines.line_delta, LineWithCursor::Generator::Empty());
  return input;
}

LineWithCursor::Generator::Vector LinesSpanView(
    std::shared_ptr<OpenBuffer> buffer,
    std::vector<BufferContentsWindow::Line> screen_lines,
    Widget::OutputProducerOptions output_producer_options,
    const size_t sections_count) {
  LineWithCursor::Generator::Vector buffer_output =
      ProduceBufferView(buffer, screen_lines, output_producer_options);

  if (buffer->Read(buffer_variables::paste_mode))
    return CenterVertically(buffer_output, output_producer_options.size.line);

  ColumnsVector columns_vector{.index_active = sections_count > 1 ? 2ul : 1ul,
                               .lines = buffer_output.size()};

  if (sections_count > 1) {
    columns_vector.push_back(
        {SectionBrackets(LineNumberDelta(screen_lines.size()),
                         SectionBracketsSide::kLeft),
         ColumnNumberDelta(1)});
  }

  LineNumberOutputProducer line_numbers(buffer, screen_lines);
  columns_vector.push_back(
      {line_numbers.Produce(LineNumberDelta(screen_lines.size())),
       line_numbers.width()});

  if (sections_count > 1 && !buffer_output.empty() &&
      buffer_output.size() > LineNumberDelta(3)) {
    buffer_output.lines.back() = {
        .inputs_hash = {},
        .generate = [original_generator = buffer_output.lines.back().generate] {
          LineWithCursor output = original_generator();
          Line::Options line_options;
          line_options.AppendString(output.line->contents(),
                                    LineModifierSet{LineModifier::DIM});
          output.line = std::make_shared<Line>(std::move(line_options));
          return output;
        }};
  }
  columns_vector.push_back(
      {std::move(buffer_output), output_producer_options.size.column});

  if (sections_count > 1) {
    columns_vector.push_back(
        {SectionBrackets(LineNumberDelta(screen_lines.size()),
                         SectionBracketsSide::kRight),
         ColumnNumberDelta(1)});
  }

  columns_vector.push_back(
      {BufferMetadataOutput(
           {.buffer = buffer,
            .screen_lines = screen_lines,
            .zoomed_out_tree = buffer->current_zoomed_out_parse_tree(
                min(output_producer_options.size.line,
                    LineNumberDelta(screen_lines.size())))}),
       std::nullopt});
  return CenterVertically(OutputFromColumnsVector(std::move(columns_vector)),
                          output_producer_options.size.line);
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

LineWithCursor::Generator::Vector ViewMultipleCursors(
    std::shared_ptr<OpenBuffer> buffer,
    Widget::OutputProducerOptions output_producer_options,
    const BufferContentsWindow::Input buffer_contents_window_input) {
  std::set<Range> sections;
  for (auto& cursor : *buffer->active_cursors()) {
    sections.insert(Range(
        LineColumn(cursor.line),
        LineColumn(min(buffer->EndLine(), cursor.line + LineNumberDelta(1)))));
  }
  bool first_run = true;
  while (first_run ||
         SumSectionsLines(sections) < min(output_producer_options.size.line,
                                          buffer->contents().size())) {
    VLOG(4) << "Expanding " << sections.size()
            << " with size: " << SumSectionsLines(sections);
    sections =
        MergeSections(ExpandSections(buffer->EndLine(), std::move(sections)));
    first_run = false;
  }

  RowsVector rows_vector{.lines = output_producer_options.size.line};
  size_t index = 0;
  for (const auto& section : sections) {
    BufferContentsWindow::Input section_input = buffer_contents_window_input;
    section_input.lines_shown = section.end.line - section.begin.line;
    section_input.status_lines = LineNumberDelta();
    // TODO: Maybe take columns into account? Ugh.
    section_input.begin = LineColumn(section.begin.line);
    Widget::OutputProducerOptions section_output_producer_options =
        output_producer_options;
    section_output_producer_options.size = LineColumnDelta(
        section_input.lines_shown, output_producer_options.size.column);
    CHECK(section_input.active_position == std::nullopt);
    VLOG(3) << "Multiple cursors section starting at: " << section_input.begin;
    LineWithCursor::Generator::Vector section_lines =
        LinesSpanView(buffer, BufferContentsWindow::Get(section_input).lines,
                      section_output_producer_options, sections.size());
    section_lines.lines.resize(section_input.lines_shown.line_delta,
                               LineWithCursor::Generator::Empty());
    rows_vector.push_back({.lines_vector = section_lines});
    if (section.Contains(buffer->position())) {
      rows_vector.index_active = index;
    }
    index++;
  }
  return OutputFromRowsVector(std::move(rows_vector));
}
}  // namespace

BufferOutputProducerOutput CreateBufferOutputProducer(
    BufferOutputProducerInput input) {
  auto buffer = input.buffer;
  if (buffer == nullptr) {
    return BufferOutputProducerOutput{
        .lines = RepeatLine(LineWithCursor(Line()),
                            input.output_producer_options.size.line),
        .view_start = input.view_start};
  }

  LOG(INFO) << "BufferWidget::RecomputeData: "
            << buffer->Read(buffer_variables::name);
  LineWithCursor::Generator::Vector status_lines;
  switch (input.status_behavior) {
    case BufferOutputProducerInput::StatusBehavior::kShow:
      status_lines = StatusOutput(
          {.status = buffer->status(),
           .buffer = buffer.get(),
           .modifiers = buffer->editor().modifiers(),
           .size = LineColumnDelta(input.output_producer_options.size.line / 4,
                                   input.output_producer_options.size.column)});
      break;
    case BufferOutputProducerInput::StatusBehavior::kIgnore:
      break;
  }

  buffer->viewers()->set_view_size(LineColumnDelta(
      input.output_producer_options.size.line - status_lines.size(),
      input.output_producer_options.size.column));

  bool paste_mode = buffer->Read(buffer_variables::paste_mode);

  BufferContentsWindow::Input buffer_contents_window_input{
      .contents = buffer->contents().copy(),
      .active_position = buffer->Read(buffer_variables::multiple_cursors)
                             ? std::optional<LineColumn>()
                             : buffer->position(),
      .active_cursors = buffer->active_cursors(),
      .line_wrap_style = buffer->Read(buffer_variables::wrap_from_content)
                             ? LineWrapStyle::kContentBased
                             : LineWrapStyle::kBreakWords,
      .symbol_characters = buffer->Read(buffer_variables::symbol_characters),
      .lines_shown = input.output_producer_options.size.line,
      .status_lines = status_lines.size(),
      .columns_shown = input.output_producer_options.size.column -
                       (paste_mode ? ColumnNumberDelta(0)
                                   : LineNumberOutputProducer::PrefixWidth(
                                         buffer->lines_size())),
      .begin = input.view_start,
      .margin_lines =
          ((buffer->child_pid() == -1 && buffer->fd() != nullptr) ||
                   buffer->Read(buffer_variables::pts)
               ? LineNumberDelta()
               : min(max(input.output_producer_options.size.line / 2 -
                             LineNumberDelta(1),
                         LineNumberDelta(0)),
                     max(LineNumberDelta(
                             ceil(buffer->Read(
                                      buffer_variables::margin_lines_ratio) *
                                  input.output_producer_options.size.line
                                      .line_delta)),
                         max(LineNumberDelta(
                                 buffer->Read(buffer_variables::margin_lines)),
                             LineNumberDelta(0)))))};

  if (auto w = ColumnNumberDelta(buffer->Read(buffer_variables::line_width));
      !buffer->Read(buffer_variables::paste_mode) && w > ColumnNumberDelta(1)) {
    buffer_contents_window_input.columns_shown =
        min(buffer_contents_window_input.columns_shown, w);
  }

  CHECK_GE(buffer_contents_window_input.margin_lines, LineNumberDelta(0));

  BufferContentsWindow window =
      BufferContentsWindow::Get(buffer_contents_window_input);
  if (window.lines.empty())
    return BufferOutputProducerOutput{
        .lines = RepeatLine(LineWithCursor(Line()),
                            input.output_producer_options.size.line),
        .view_start = {}};

  LineColumnDelta total_size = input.output_producer_options.size;
  input.output_producer_options.size = LineColumnDelta(
      max(LineNumberDelta(),
          input.output_producer_options.size.line - status_lines.size()),
      buffer_contents_window_input.columns_shown);
  input.view_start = window.view_start;

  BufferOutputProducerOutput output{
      .lines = buffer->Read(buffer_variables::multiple_cursors)
                   ? ViewMultipleCursors(buffer, input.output_producer_options,
                                         buffer_contents_window_input)
                   : LinesSpanView(buffer, window.lines,
                                   input.output_producer_options, 1),
      .view_start = window.view_start};

  CHECK_EQ(output.lines.size(), input.output_producer_options.size.line);
  if (!status_lines.size().IsZero()) {
    RowsVector::Row buffer_row = {
        .lines_vector =
            CenterOutput(std::move(output.lines), total_size.column)};
    RowsVector::Row status_row = {
        .lines_vector =
            CenterOutput(std::move(status_lines), total_size.column)};

    size_t buffer_index = 0;
    size_t status_index = 1;
    switch (window.status_position) {
      case BufferContentsWindow::StatusPosition::kTop:
        status_index = 0;
        buffer_index = 1;
        break;
      case BufferContentsWindow::StatusPosition::kBottom:
        buffer_index = 0;
        status_index = 1;
        break;
    }

    RowsVector rows_vector{
        .index_active = buffer->status().GetType() == Status::Type::kPrompt
                            ? status_index
                            : buffer_index,
        .lines = total_size.line};
    rows_vector.rows.resize(2);
    rows_vector.rows[buffer_index] = std::move(buffer_row);
    rows_vector.rows[status_index] = std::move(status_row);

    output.lines = OutputFromRowsVector(std::move(rows_vector));
  }
  return output;
}

BufferWidget::BufferWidget(Options options) : options_(std::move(options)) {}

LineWithCursor::Generator::Vector BufferWidget::CreateOutput(
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
  BufferOutputProducerOutput output =
      CreateBufferOutputProducer(std::move(input));
  // We avoid updating the desired view_start while the buffer is still being
  // read.
  if (buffer != nullptr &&
      buffer->lines_size() >= buffer->position().line.ToDelta() &&
      (buffer->child_pid() != -1 || buffer->fd() == nullptr)) {
    buffer->Set(buffer_variables::view_start, output.view_start);
  }

  if (options_.position_in_parent.has_value()) {
    RowsVector nested_rows{.index_active = 1, .lines = options.size.line};
    FrameOutputProducerOptions frame_options;
    frame_options.title =
        buffer == nullptr ? L"" : buffer->Read(buffer_variables::name);

    frame_options.position_in_parent = options_.position_in_parent.value();
    if (options_.is_active &&
        options.main_cursor_behavior ==
            OutputProducerOptions::MainCursorBehavior::kIgnore) {
      frame_options.active_state =
          FrameOutputProducerOptions::ActiveState::kActive;
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
        {.lines_vector =
             RepeatLine(LineWithCursor(FrameLine(std::move(frame_options))),
                        LineNumberDelta(1))});

    options.size.line -= LineNumberDelta(1);
    options.main_cursor_behavior =
        options_.is_active
            ? options.main_cursor_behavior
            : Widget::OutputProducerOptions::MainCursorBehavior::kHighlight;

    if (add_left_frame) {
      output.lines = AddLeftFrame(
          std::move(output.lines), options.size.line,
          options_.is_active
              ? LineModifierSet{LineModifier::BOLD, LineModifier::CYAN}
              : LineModifierSet{LineModifier::DIM});
    }
    CHECK_EQ(output.lines.size(), options.size.line);
    nested_rows.push_back({.lines_vector = std::move(output.lines)});
    output.lines = OutputFromRowsVector(std::move(nested_rows));
  }

  return output.lines;
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
