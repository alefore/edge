#include "src/buffer_widget.h"

#include <glog/logging.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <iostream>

#include "src/buffer.h"
#include "src/buffer_contents_view_layout.h"
#include "src/buffer_flags.h"
#include "src/buffer_metadata_output_producer.h"
#include "src/buffer_output_producer.h"
#include "src/buffer_variables.h"
#include "src/columns_vector.h"
#include "src/editor.h"
#include "src/frame_output_producer.h"
#include "src/horizontal_center_output_producer.h"
#include "src/infrastructure/tracker.h"
#include "src/language/lazy_string/char_buffer.h"
#include "src/language/lazy_string/padding.h"
#include "src/language/wstring.h"
#include "src/line_number_output_producer.h"
#include "src/section_brackets_producer.h"
#include "src/status_output_producer.h"
#include "src/tests/tests.h"
#include "src/widget.h"

namespace afc::editor {
namespace {
using infrastructure::Tracker;
using language::MakeNonNullShared;
using language::NonNull;
using language::VisitPointer;
using language::lazy_string::ColumnNumber;
using language::lazy_string::ColumnNumberDelta;
using language::lazy_string::Padding;

namespace gc = language::gc;

static const auto kTopFrameLines = LineNumberDelta(1);
static const auto kStatusFrameLines = LineNumberDelta(1);

LineWithCursor ProducerForString(std::wstring src, LineModifierSet modifiers) {
  LineBuilder options;
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
    LineNumberDelta total_lines, BufferDisplayData& display_data) {
  if (input.size() + status_lines < total_lines) {
    LineNumberDelta prefix_size =
        std::min((total_lines - input.size()) / 2,
                 total_lines - status_lines - input.size());
    prefix_size =
        std::min(display_data.min_vertical_prefix_size().value_or(prefix_size),
                 prefix_size);
    input.PrependEmptyLines(prefix_size);
    display_data.AddVerticalPrefixSize(prefix_size);
  }

  input.resize(total_lines - status_lines);
  return input;
}

LineWithCursor::Generator::Vector LinesSpanView(
    const OpenBuffer& buffer,
    const std::vector<BufferContentsViewLayout::Line>& screen_lines,
    const Widget::OutputProducerOptions& output_producer_options,
    const size_t sections_count) {
  static Tracker tracker(L"LinesSpanView");
  auto call = tracker.Call();

  LineWithCursor::Generator::Vector buffer_output =
      ProduceBufferView(buffer, screen_lines, output_producer_options);

  if (buffer.Read(buffer_variables::paste_mode)) return buffer_output;

  ColumnsVector columns_vector;

  if (sections_count > 1) {
    ++columns_vector.index_active;
    columns_vector.push_back(
        {.lines = SectionBrackets(LineNumberDelta(screen_lines.size()),
                                  SectionBracketsSide::kLeft),
         .width = ColumnNumberDelta(1)});
  }

  LineWithCursor::Generator::Vector line_numbers =
      LineNumberOutput(buffer, screen_lines);
  ++columns_vector.index_active;
  columns_vector.push_back(
      {.lines = line_numbers, .width = line_numbers.width});

  if (sections_count > 1 && !buffer_output.empty() &&
      buffer_output.size() > LineNumberDelta(3)) {
    buffer_output.lines.back() = {
        .inputs_hash = {},
        .generate = [original_generator = buffer_output.lines.back().generate] {
          LineWithCursor output = original_generator();
          LineBuilder line_options;
          line_options.AppendString(output.line->contents(),
                                    LineModifierSet{LineModifier::kDim});
          output.line = MakeNonNullShared<Line>(std::move(line_options));
          return output;
        }};
  }

  if (buffer.Read(buffer_variables::view_center_lines)) {
    const ColumnNumberDelta width = output_producer_options.size.column;
    for (LineWithCursor::Generator& line : buffer_output.lines) {
      line = {
          .inputs_hash = line.inputs_hash,
          .generate = [width, original_generator = std::move(line.generate)] {
            LineWithCursor output = original_generator();
            if (output.line->EndColumn().ToDelta() >= width) return output;
            const ColumnNumberDelta padding_size =
                (width - output.line->EndColumn().ToDelta() +
                 ColumnNumberDelta(1)) /
                2;
            LineBuilder line_options;
            line_options.AppendString(Padding(padding_size, L' '));
            if (output.cursor.has_value()) {
              output.cursor = *output.cursor + padding_size;
            }
            line_options.Append(
                std::move(output.line.value()).GetLineBuilder());
            output.line = MakeNonNullShared<Line>(std::move(line_options));
            return output;
          }};
    }
  }

  columns_vector.push_back({.lines = std::move(buffer_output),
                            .width = output_producer_options.size.column});

  if (sections_count > 1) {
    columns_vector.push_back(
        {.lines = SectionBrackets(LineNumberDelta(screen_lines.size()),
                                  SectionBracketsSide::kRight),
         .width = ColumnNumberDelta(1)});
  }

  NonNull<std::shared_ptr<const ParseTree>> zoomed_out_tree =
      buffer.current_zoomed_out_parse_tree(
          std::min(output_producer_options.size.line,
                   LineNumberDelta(screen_lines.size())));
  columns_vector.push_back(BufferMetadataOutput(
      BufferMetadataOutputOptions{.buffer = buffer,
                                  .screen_lines = screen_lines,
                                  .zoomed_out_tree = zoomed_out_tree.value()}));
  return OutputFromColumnsVector(std::move(columns_vector));
}

std::set<Range> MergeSections(std::set<Range> input) {
  std::set<Range> output;
  for (auto& section : input) {
    if (!output.empty()) {
      if (auto result = output.rbegin()->Union(section);
          std::holds_alternative<Range>(result)) {
        output.erase(--output.end());
        output.insert(std::get<Range>(result));
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
              LineColumn(std::min(end_line + LineNumberDelta(1),
                                  section.end.line + kMargin))));
  }
  return output;
}

LineWithCursor::Generator::Vector ViewMultipleCursors(
    const OpenBuffer& buffer,
    const Widget::OutputProducerOptions& output_producer_options,
    const BufferContentsViewLayout::Input& buffer_contents_window_input) {
  std::set<Range> sections;
  for (auto& cursor : buffer.active_cursors()) {
    sections.insert(
        Range(LineColumn(cursor.line),
              LineColumn(std::min(buffer.EndLine(),
                                  cursor.line + LineNumberDelta(1)))));
  }
  bool first_run = true;
  while (first_run || SumSectionsLines(sections) <
                          std::min(output_producer_options.size.line,
                                   buffer.contents().size())) {
    VLOG(4) << "Expanding " << sections.size()
            << " with size: " << SumSectionsLines(sections);
    sections =
        MergeSections(ExpandSections(buffer.EndLine(), std::move(sections)));
    first_run = false;
  }

  LineWithCursor::Generator::Vector output;
  for (const auto& section : sections) {
    BufferContentsViewLayout::Input section_input =
        buffer_contents_window_input;
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
    LineWithCursor::Generator::Vector section_lines = LinesSpanView(
        buffer, BufferContentsViewLayout::Get(section_input).lines,
        section_output_producer_options, sections.size());
    section_lines.lines.resize(section_input.lines_shown.read(),
                               LineWithCursor::Generator::Empty());
    if (!section.Contains(buffer.position())) section_lines.RemoveCursor();
    output.Append(section_lines);
  }
  return output;
}
}  // namespace

BufferOutputProducerOutput CreateBufferOutputProducer(
    BufferOutputProducerInput input) {
  const OpenBuffer& buffer = input.buffer;

  LOG(INFO) << "BufferWidget::RecomputeData: "
            << buffer.Read(buffer_variables::name);
  LineWithCursor::Generator::Vector status_lines;
  switch (input.status_behavior) {
    case BufferOutputProducerInput::StatusBehavior::kShow:
      status_lines = StatusOutput(
          {.status = buffer.status(),
           .buffer = &buffer,
           .modifiers = buffer.editor().modifiers(),
           .size = LineColumnDelta(input.output_producer_options.size.line / 4,
                                   input.output_producer_options.size.column)});
      break;
    case BufferOutputProducerInput::StatusBehavior::kIgnore:
      break;
  }

  input.buffer_display_data.view_size().Set(LineColumnDelta(
      input.output_producer_options.size.line - status_lines.size(),
      input.output_producer_options.size.column));

  bool paste_mode = buffer.Read(buffer_variables::paste_mode);

  BufferContentsViewLayout::Input buffer_contents_window_input{
      .contents = buffer.contents(),
      .active_position = buffer.Read(buffer_variables::multiple_cursors)
                             ? std::optional<LineColumn>()
                             : buffer.position(),
      .active_cursors = buffer.active_cursors(),
      .line_wrap_style = buffer.Read(buffer_variables::wrap_from_content)
                             ? LineWrapStyle::kContentBased
                             : LineWrapStyle::kBreakWords,
      .symbol_characters = buffer.Read(buffer_variables::symbol_characters),
      .lines_shown = input.output_producer_options.size.line,
      .status_lines = status_lines.size(),
      .columns_shown =
          input.output_producer_options.size.column -
          (paste_mode ? ColumnNumberDelta(0)
                      : LineNumberOutputWidth(buffer.lines_size())),
      .begin = input.view_start,
      .margin_lines =
          ((buffer.child_pid() == -1 && buffer.fd() != nullptr) ||
                   buffer.Read(buffer_variables::pts)
               ? LineNumberDelta()
               : std::min(
                     std::max(input.output_producer_options.size.line / 2 -
                                  LineNumberDelta(1),
                              LineNumberDelta(0)),
                     std::max(
                         LineNumberDelta(ceil(
                             buffer.Read(buffer_variables::margin_lines_ratio) *
                             input.output_producer_options.size.line.read())),
                         std::max(LineNumberDelta(buffer.Read(
                                      buffer_variables::margin_lines)),
                                  LineNumberDelta(0)))))};

  if (auto w = ColumnNumberDelta(buffer.Read(buffer_variables::line_width));
      !buffer.Read(buffer_variables::paste_mode) && w > ColumnNumberDelta(1)) {
    buffer_contents_window_input.columns_shown =
        std::min(buffer_contents_window_input.columns_shown, w);
  }

  CHECK_GE(buffer_contents_window_input.margin_lines, LineNumberDelta(0));

  BufferContentsViewLayout window =
      BufferContentsViewLayout::Get(buffer_contents_window_input);
  if (window.lines.empty())
    return BufferOutputProducerOutput{
        .lines = RepeatLine({}, input.output_producer_options.size.line),
        .view_start = {}};

  LineColumnDelta total_size = input.output_producer_options.size;
  input.output_producer_options.size = LineColumnDelta(
      std::max(LineNumberDelta(),
               input.output_producer_options.size.line - status_lines.size()),
      buffer_contents_window_input.columns_shown);
  input.view_start = window.view_start;

  BufferOutputProducerOutput output{
      .lines = buffer.Read(buffer_variables::multiple_cursors)
                   ? ViewMultipleCursors(buffer, input.output_producer_options,
                                         buffer_contents_window_input)
                   : LinesSpanView(buffer, window.lines,
                                   input.output_producer_options, 1),
      .view_start = window.view_start};
  if (!buffer.Read(buffer_variables::paste_mode))
    input.buffer_display_data.AddDisplayWidth(output.lines.width);

  output.lines = CenterOutput(std::move(output.lines), total_size.column,
                              GetBufferFlag(buffer));
  output.lines = CenterVertically(std::move(output.lines), status_lines.size(),
                                  total_size.line, input.buffer_display_data);
  CHECK_EQ(output.lines.size(), total_size.line - status_lines.size());

  if (!status_lines.size().IsZero()) {
    output.lines.width = std::max(
        output.lines.width, input.buffer_display_data.max_display_width());
    (buffer.status().GetType() == Status::Type::kPrompt ? output.lines
                                                        : status_lines)
        .RemoveCursor();
    output.lines.Append(std::move(status_lines));
  }
  return output;
}

BufferWidget::BufferWidget(Options options) : options_(std::move(options)) {}

LineWithCursor::Generator::Vector BufferWidget::CreateOutput(
    OutputProducerOptions options) const {
  static Tracker tracker(L"BufferWidget::CreateOutput");
  auto call = tracker.Call();

  return VisitPointer(
      options_.buffer.Lock(),
      [&](gc::Root<OpenBuffer> buffer) {
        if (buffer.ptr()->Read(buffer_variables::reload_on_display))
          buffer.ptr()->Reload();
        BufferOutputProducerInput input{
            .output_producer_options = options,
            .buffer = buffer.ptr().value(),
            .buffer_display_data = buffer.ptr()->display_data(),
            .view_start = view_start()};
        if (options_.position_in_parent.has_value()) {
          input.output_producer_options.size.line = std::max(
              LineNumberDelta(),
              input.output_producer_options.size.line - kTopFrameLines);
        }
        BufferOutputProducerOutput output =
            CreateBufferOutputProducer(std::move(input));
        // We avoid updating the desired view_start while the buffer is still
        // being read.
        if (buffer.ptr()->lines_size() >=
                buffer.ptr()->position().line.ToDelta() &&
            (buffer.ptr()->child_pid() != -1 ||
             buffer.ptr()->fd() == nullptr)) {
          buffer.ptr()->Set(buffer_variables::view_start, output.view_start);
        }

        if (options_.position_in_parent.has_value()) {
          FrameOutputProducerOptions frame_options;
          frame_options.title = buffer.ptr()->Read(buffer_variables::name);

          frame_options.position_in_parent =
              options_.position_in_parent.value();
          if (options_.is_active &&
              options.main_cursor_display ==
                  OutputProducerOptions::MainCursorDisplay::kActive) {
            frame_options.active_state =
                FrameOutputProducerOptions::ActiveState::kActive;
          }

          frame_options.extra_information =
              OpenBuffer::FlagsToString(buffer.ptr()->Flags());
          frame_options.width = ColumnNumberDelta(
              buffer.ptr()->Read(buffer_variables::line_width));
          bool add_left_frame =
              !buffer.ptr()->Read(buffer_variables::paste_mode);

          frame_options.prefix =
              (options.size.line > kTopFrameLines && add_left_frame) ? L"╭"
                                                                     : L"─";

          auto frame_lines =
              RepeatLine({.line = MakeNonNullShared<Line>(
                              FrameLine(std::move(frame_options)))},
                         LineNumberDelta(1));

          if (add_left_frame) {
            output.lines = AddLeftFrame(
                std::move(output.lines),
                options_.is_active
                    ? LineModifierSet{LineModifier::kBold, LineModifier::kCyan}
                    : LineModifierSet{LineModifier::kDim});
          }
          frame_lines.Append(std::move(output.lines));
          output.lines = std::move(frame_lines);
        }

        return output.lines;
      },
      [&] { return RepeatLine({}, options.size.line); });
}

LineNumberDelta BufferWidget::MinimumLines() const {
  return VisitPointer(
      Lock(),
      [&](gc::Root<OpenBuffer> buffer) {
        return (options_.position_in_parent.has_value() ? kTopFrameLines
                                                        : LineNumberDelta(0)) +
               std::max(
                   LineNumberDelta(0),
                   std::min(
                       buffer.ptr()->lines_size(),
                       LineNumberDelta(buffer.ptr()->Read(
                           buffer_variables::buffer_list_context_lines)))) +
               kStatusFrameLines;
      },
      [] { return LineNumberDelta(0); });
}

LineNumberDelta BufferWidget::DesiredLines() const {
  return VisitPointer(
      Lock(),
      [&](gc::Root<OpenBuffer> buffer) {
        return (options_.position_in_parent.has_value() ? kTopFrameLines
                                                        : LineNumberDelta(0)) +
               buffer.ptr()->lines_size() + kStatusFrameLines;
      },
      [] { return LineNumberDelta(0); });
}

LineColumn BufferWidget::view_start() const {
  return VisitPointer(
      Lock(),
      [](gc::Root<OpenBuffer> buffer) {
        return buffer.ptr()->Read(buffer_variables::view_start);
      },
      [] { return LineColumn(); });
}

std::optional<gc::Root<OpenBuffer>> BufferWidget::Lock() const {
  return options_.buffer.Lock();
}

void BufferWidget::SetBuffer(gc::WeakPtr<OpenBuffer> buffer) {
  options_.buffer = std::move(buffer);
}

}  // namespace afc::editor
