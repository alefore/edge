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
#include "src/columns_vector.h"
#include "src/editor.h"
#include "src/frame_output_producer.h"
#include "src/horizontal_center_output_producer.h"
#include "src/infrastructure/tracker.h"
#include "src/language/wstring.h"
#include "src/line_number_output_producer.h"
#include "src/line_scroll_control.h"
#include "src/section_brackets_producer.h"
#include "src/status_output_producer.h"
#include "src/tests/tests.h"
#include "src/widget.h"

namespace afc::editor {
namespace {
using infrastructure::Tracker;
using language::MakeNonNullShared;
using language::NonNull;

static const auto kTopFrameLines = LineNumberDelta(1);
static const auto kStatusFrameLines = LineNumberDelta(1);

LineWithCursor ProducerForString(std::wstring src, LineModifierSet modifiers) {
  Line::Options options;
  options.AppendString(std::move(src), std::move(modifiers));
  return LineWithCursor{.line = MakeNonNullShared<Line>(std::move(options))};
}

LineWithCursor::Generator::Vector AddLeftFrame(
    LineWithCursor::Generator::Vector lines, LineModifierSet modifiers) {
  if (lines.size().IsZero()) return {};

  ColumnsVector columns_vector{.index_active = 1};

  LineWithCursor::Generator::Vector rows;
  if (lines.size() > LineNumberDelta(1)) {
    rows = RepeatLine(ProducerForString(L"│", modifiers),
                      lines.size() - LineNumberDelta(1));
  }
  rows.Append(
      RepeatLine(ProducerForString(L"╰", modifiers), LineNumberDelta(1)));

  columns_vector.push_back(
      {.lines = std::move(rows), .width = ColumnNumberDelta(1)});

  columns_vector.push_back({.lines = lines});

  return OutputFromColumnsVector(std::move(columns_vector));
}

LineWithCursor::Generator::Vector CenterVertically(
    LineWithCursor::Generator::Vector input, LineNumberDelta status_lines,
    LineNumberDelta total_lines,
    BufferContentsWindow::StatusPosition status_position) {
  if (input.size() + status_lines < total_lines) {
    LineNumberDelta prefix_size;
    switch (status_position) {
      case BufferContentsWindow::StatusPosition::kTop:
        prefix_size = max(LineNumberDelta(),
                          (total_lines - input.size()) / 2 - status_lines);
        break;
      case BufferContentsWindow::StatusPosition::kBottom:
        prefix_size = min((total_lines - input.size()) / 2,
                          total_lines - status_lines - input.size());
        break;
    }
    input.PrependEmptyLines(prefix_size);
  }
  input.resize(total_lines - status_lines);
  return input;
}

LineWithCursor::Generator::Vector LinesSpanView(
    const OpenBuffer& buffer,
    const std::vector<BufferContentsWindow::Line>& screen_lines,
    const Widget::OutputProducerOptions& output_producer_options,
    const size_t sections_count) {
  static Tracker tracker(L"LinesSpanView");
  auto call = tracker.Call();

  LineWithCursor::Generator::Vector buffer_output =
      ProduceBufferView(buffer, screen_lines, output_producer_options);

  if (buffer.Read(buffer_variables::paste_mode)) return buffer_output;

  ColumnsVector columns_vector{.index_active = sections_count > 1 ? 2ul : 1ul};

  if (sections_count > 1) {
    columns_vector.push_back(
        {.lines = SectionBrackets(LineNumberDelta(screen_lines.size()),
                                  SectionBracketsSide::kLeft),
         .width = ColumnNumberDelta(1)});
  }

  LineWithCursor::Generator::Vector line_numbers =
      LineNumberOutput(buffer, screen_lines);
  columns_vector.push_back(
      {.lines = line_numbers, .width = line_numbers.width});

  if (sections_count > 1 && !buffer_output.empty() &&
      buffer_output.size() > LineNumberDelta(3)) {
    buffer_output.lines.back() = {
        .inputs_hash = {},
        .generate = [original_generator = buffer_output.lines.back().generate] {
          LineWithCursor output = original_generator();
          Line::Options line_options;
          line_options.AppendString(output.line->contents(),
                                    LineModifierSet{LineModifier::DIM});
          output.line = MakeNonNullShared<Line>(std::move(line_options));
          return output;
        }};
  }
  columns_vector.push_back({.lines = std::move(buffer_output),
                            .width = output_producer_options.size.column});

  if (sections_count > 1) {
    columns_vector.push_back(
        {.lines = SectionBrackets(LineNumberDelta(screen_lines.size()),
                                  SectionBracketsSide::kRight),
         .width = ColumnNumberDelta(1)});
  }

  columns_vector.push_back(BufferMetadataOutput(
      {.buffer = buffer,
       .screen_lines = screen_lines,
       .zoomed_out_tree = buffer
                              .current_zoomed_out_parse_tree(
                                  min(output_producer_options.size.line,
                                      LineNumberDelta(screen_lines.size())))
                              .get()}));
  return OutputFromColumnsVector(std::move(columns_vector));
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
    const OpenBuffer& buffer,
    const Widget::OutputProducerOptions& output_producer_options,
    const BufferContentsWindow::Input& buffer_contents_window_input) {
  std::set<Range> sections;
  for (auto& cursor : buffer.active_cursors()) {
    sections.insert(Range(
        LineColumn(cursor.line),
        LineColumn(min(buffer.EndLine(), cursor.line + LineNumberDelta(1)))));
  }
  bool first_run = true;
  while (first_run ||
         SumSectionsLines(sections) <
             min(output_producer_options.size.line, buffer.contents().size())) {
    VLOG(4) << "Expanding " << sections.size()
            << " with size: " << SumSectionsLines(sections);
    sections =
        MergeSections(ExpandSections(buffer.EndLine(), std::move(sections)));
    first_run = false;
  }

  LineWithCursor::Generator::Vector output;
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
    if (!section.Contains(buffer.position())) section_lines.RemoveCursor();
    output.Append(section_lines);
  }
  return output;
}
}  // namespace

BufferOutputProducerOutput CreateBufferOutputProducer(
    BufferOutputProducerInput input) {
  NonNull<std::shared_ptr<OpenBuffer>> buffer = input.buffer;

  LOG(INFO) << "BufferWidget::RecomputeData: "
            << buffer->Read(buffer_variables::name);
  LineWithCursor::Generator::Vector status_lines;
  switch (input.status_behavior) {
    case BufferOutputProducerInput::StatusBehavior::kShow:
      status_lines = CenterOutput(
          StatusOutput({.status = buffer->status(),
                        .buffer = buffer.get(),
                        .modifiers = buffer->editor().modifiers(),
                        .size = LineColumnDelta(
                            input.output_producer_options.size.line / 4,
                            input.output_producer_options.size.column)}),
          input.output_producer_options.size.column);
      break;
    case BufferOutputProducerInput::StatusBehavior::kIgnore:
      break;
  }

  buffer->view_size().Set(LineColumnDelta(
      input.output_producer_options.size.line - status_lines.size(),
      input.output_producer_options.size.column));

  bool paste_mode = buffer->Read(buffer_variables::paste_mode);

  BufferContentsWindow::Input buffer_contents_window_input{
      .contents = buffer->contents(),
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
      .columns_shown =
          input.output_producer_options.size.column -
          (paste_mode ? ColumnNumberDelta(0)
                      : LineNumberOutputWidth(buffer->lines_size())),
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
        .lines = RepeatLine({}, input.output_producer_options.size.line),
        .view_start = {}};

  LineColumnDelta total_size = input.output_producer_options.size;
  input.output_producer_options.size = LineColumnDelta(
      max(LineNumberDelta(),
          input.output_producer_options.size.line - status_lines.size()),
      buffer_contents_window_input.columns_shown);
  input.view_start = window.view_start;

  if (buffer->Read(buffer_variables::reload_on_display)) buffer->Reload();

  BufferOutputProducerOutput output{
      .lines = CenterVertically(
          buffer->Read(buffer_variables::multiple_cursors)
              ? ViewMultipleCursors(*buffer, input.output_producer_options,
                                    buffer_contents_window_input)
              : LinesSpanView(*buffer, window.lines,
                              input.output_producer_options, 1),
          status_lines.size(), total_size.line, window.status_position),
      .view_start = window.view_start};
  CHECK_EQ(output.lines.size(), total_size.line - status_lines.size());

  if (!status_lines.size().IsZero()) {
    output.lines = CenterOutput(std::move(output.lines), total_size.column);
    status_lines = CenterOutput(std::move(status_lines), total_size.column);
    (buffer->status().GetType() == Status::Type::kPrompt ? output.lines
                                                         : status_lines)
        .RemoveCursor();

    switch (window.status_position) {
      case BufferContentsWindow::StatusPosition::kTop:
        status_lines.Append(std::move(output.lines));
        output.lines = std::move(status_lines);
        break;
      case BufferContentsWindow::StatusPosition::kBottom:
        output.lines.Append(std::move(status_lines));
        break;
    }

    CHECK_EQ(output.lines.size(), total_size.line);
  }
  return output;
}

BufferWidget::BufferWidget(Options options) : options_(std::move(options)) {}

LineWithCursor::Generator::Vector BufferWidget::CreateOutput(
    OutputProducerOptions options) const {
  static Tracker tracker(L"BufferWidget::CreateOutput");
  auto call = tracker.Call();

  std::shared_ptr<OpenBuffer> buffer = options_.buffer.lock();
  if (buffer == nullptr) {
    return RepeatLine({}, options.size.line);
  }

  BufferOutputProducerInput input{
      .output_producer_options = options,
      // TODO(easy, 2022-04-30): Get rid of Unsafe.
      .buffer = NonNull<std::shared_ptr<OpenBuffer>>::Unsafe(buffer),
      .view_start = view_start()};
  if (options_.position_in_parent.has_value()) {
    input.output_producer_options.size.line =
        max(LineNumberDelta(),
            input.output_producer_options.size.line - kTopFrameLines);
  }
  BufferOutputProducerOutput output =
      CreateBufferOutputProducer(std::move(input));
  // We avoid updating the desired view_start while the buffer is still being
  // read.
  // TODO(easy, 2022-04-30): Get rid of check against nullptr.
  if (buffer != nullptr &&
      buffer->lines_size() >= buffer->position().line.ToDelta() &&
      (buffer->child_pid() != -1 || buffer->fd() == nullptr)) {
    buffer->Set(buffer_variables::view_start, output.view_start);
  }

  if (options_.position_in_parent.has_value()) {
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

    auto frame_lines = RepeatLine(
        {.line = MakeNonNullShared<Line>(FrameLine(std::move(frame_options)))},
        LineNumberDelta(1));

    if (add_left_frame) {
      output.lines = AddLeftFrame(
          std::move(output.lines),
          options_.is_active
              ? LineModifierSet{LineModifier::BOLD, LineModifier::CYAN}
              : LineModifierSet{LineModifier::DIM});
    }
    frame_lines.Append(std::move(output.lines));
    output.lines = std::move(frame_lines);
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
