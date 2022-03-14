#include "src/line_scroll_control.h"

#include <glog/logging.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <iostream>

#include "src/buffer_contents.h"
#include "src/buffer_output_producer.h"
#include "src/buffer_variables.h"
#include "src/buffer_widget.h"
#include "src/char_buffer.h"
#include "src/line_output.h"
#include "src/tests/tests.h"
#include "src/vertical_split_output_producer.h"
#include "src/widget.h"
#include "src/wstring.h"

namespace afc::editor {
LineScrollControl::Reader::Reader(ConstructorAccessTag,
                                  std::shared_ptr<LineScrollControl> parent)
    : parent_(std::move(parent)) {
  CHECK(parent_ != nullptr);
}

LineScrollControl::LineScrollControl(ConstructorAccessTag, Options options)
    : options_([](Options options) {
        options.begin = std::min(options.begin, options.active_position);
        options.begin = std::max(
            options.begin,
            LineColumn(options.active_position.line.MinusHandlingOverflow(
                options.lines_shown)));
        return options;
      }(std::move(options))),
      cursors_([=]() {
        CHECK(options_.contents != nullptr);
        std::map<LineNumber, std::set<ColumnNumber>> cursors;
        for (auto& cursor : *options_.active_cursors) {
          cursors[cursor.line].insert(cursor.column);
        }
        return cursors;
      }()),
      ranges_(ComputeRanges()) {}

std::unique_ptr<LineScrollControl::Reader> LineScrollControl::NewReader() {
  auto output = std::make_unique<LineScrollControl::Reader>(
      Reader::ConstructorAccessTag(), shared_from_this());
  readers_.push_back(output.get());
  return output;
}

std::optional<Range> LineScrollControl::Reader::GetRange() const {
  switch (state_) {
    case State::kDone:
      return std::nullopt;
    case State::kProcessing:
      return parent_->range();
  }
  LOG(FATAL) << "GetRange didn't handle all cases.";
  return std::nullopt;
}

bool LineScrollControl::Reader::HasActiveCursor() const {
  CHECK(state_ == State::kProcessing);
  return parent_->CurrentRangeContainsPosition(
      parent_->options_.active_position);
}

std::set<ColumnNumber> LineScrollControl::Reader::GetCurrentCursors() const {
  CHECK(state_ == State::kProcessing);
  Range range = parent_->range();

  LineNumber line = range.begin.line;
  auto it = parent_->cursors_.find(line);
  if (it == parent_->cursors_.end()) {
    return {};
  }
  std::set<ColumnNumber> output;
  for (auto& column : it->second) {
    if (parent_->CurrentRangeContainsPosition(LineColumn(line, column)))
      output.insert(column);
  }
  return output;
}

std::list<ColumnRange> LineScrollControl::ComputeBreaks(LineNumber line) const {
  return BreakLineForOutput(*options_.contents->at(line),
                            options_.columns_shown, options_.line_wrap_style,
                            options_.symbol_characters);
}

namespace {
// If the position is before the ranges, returns 0. If the position is after
// the ranges, returns the last line.
LineNumber FindPositionInScreen(const std::list<Range> ranges,
                                LineColumn position) {
  CHECK(!ranges.empty());
  if (position < ranges.front().begin) {
    return LineNumber();
  }

  if (ranges.back().end < position) {
    return LineNumber(ranges.size()) - LineNumberDelta(1);  // Optimization.
  }

  LineNumber output;
  for (auto it = std::next(ranges.cbegin()); it != ranges.cend(); ++it) {
    if (position < it->begin) {
      return output;
    }
    ++output;
  }
  CHECK_EQ(LineNumber(ranges.size()) - LineNumberDelta(1), output);
  return output;
}

const bool screen_lines_to_position_tests_registration = tests::Register(
    L"FindPositionInScreen",
    {{.name = L"BeforeFirst",
      .callback =
          [] {
            CHECK_EQ(FindPositionInScreen(
                         std::list<Range>({
                             Range::InLine(LineNumber(10), ColumnNumber(20),
                                           ColumnNumberDelta(8)),
                             Range::InLine(LineNumber(11), ColumnNumber(0),
                                           ColumnNumberDelta(10)),
                         }),
                         LineColumn(LineNumber(4), ColumnNumber(25))),
                     LineNumber());
          }},
     {.name = L"InFirst",
      .callback =
          [] {
            CHECK_EQ(FindPositionInScreen(
                         std::list<Range>(
                             {Range::InLine(LineNumber(10), ColumnNumber(20),
                                            ColumnNumberDelta(8))}),
                         LineColumn(LineNumber(10), ColumnNumber(25))),
                     LineNumber(0));
          }},
     {.name = L"BeforeSecond",
      .callback =
          [] {
            CHECK_EQ(FindPositionInScreen(
                         std::list<Range>(
                             {Range::InLine(LineNumber(10), ColumnNumber(20),
                                            ColumnNumberDelta(8)),
                              Range::InLine(LineNumber(11), ColumnNumber(0),
                                            ColumnNumberDelta(10))}),
                         LineColumn(LineNumber(10), ColumnNumber(95))),
                     LineNumber(0));
          }},
     {.name = L"InSecond",
      .callback =
          [] {
            CHECK_EQ(FindPositionInScreen(
                         std::list<Range>({
                             Range::InLine(LineNumber(10), ColumnNumber(20),
                                           ColumnNumberDelta(8)),
                             Range::InLine(LineNumber(11), ColumnNumber(0),
                                           ColumnNumberDelta(10)),
                         }),
                         LineColumn(LineNumber(11), ColumnNumber(2))),
                     LineNumber(1));
          }},
     {.name = L"AfterLast", .callback = [] {
        CHECK_EQ(FindPositionInScreen(
                     std::list<Range>({
                         Range::InLine(LineNumber(10), ColumnNumber(20),
                                       ColumnNumberDelta(8)),
                         Range::InLine(LineNumber(11), ColumnNumber(0),
                                       ColumnNumberDelta(10)),
                     }),
                     LineColumn(LineNumber(12))),
                 LineNumber(1));
      }}});
}  // namespace

std::list<Range> LineScrollControl::PrependLines(
    LineNumber line, LineNumberDelta lines_desired,
    std::list<Range> output) const {
  std::list<ColumnRange> line_breaks = ComputeBreaks(line);
  if (line == output.front().begin.line) {
    line_breaks.remove_if([&](const ColumnRange& r) {
      return r.end > output.front().begin.column;
    });
  }
  std::list<Range> ranges_to_insert;
  for (auto& r : line_breaks) {
    ranges_to_insert.push_back(Range::InLine(line, r.begin, r.end - r.begin));
  }
  while (LineNumberDelta(ranges_to_insert.size()) > lines_desired) {
    ranges_to_insert.pop_front();
  }
  output.insert(output.begin(), ranges_to_insert.begin(),
                ranges_to_insert.end());
  return output;
}

std::list<Range> LineScrollControl::AdjustToHonorMargin(
    std::list<Range> output) const {
  CHECK(!output.empty());
  if (options_.margin_lines > options_.lines_shown / 2 ||
      options_.begin == LineColumn()) {
    return output;
  }

  LineNumber position_line =
      FindPositionInScreen(output, options_.active_position);
  auto lines_desired = [&] {
    return std::max(std::max(LineNumberDelta(0),
                             options_.margin_lines - position_line.ToDelta()),
                    options_.lines_shown - LineNumberDelta(output.size()));
  };
  for (LineNumber line = options_.begin.column.IsZero()
                             ? options_.begin.line - LineNumberDelta(1)
                             : options_.begin.line;
       lines_desired() > LineNumberDelta(); --line) {
    LineNumberDelta original_length(output.size());
    auto lines_to_insert = lines_desired();
    output = PrependLines(line, lines_to_insert, std::move(output));
    CHECK_GE(LineNumberDelta(output.size()), original_length);
    position_line += LineNumberDelta(output.size()) - original_length;
    if (line.IsZero()) break;
  }
  return output;
}

std::list<Range> LineScrollControl::ComputeRanges() const {
  std::list<Range> output;
  for (LineNumber line = options_.begin.line;
       LineNumberDelta(output.size()) < options_.lines_shown; ++line) {
    if (line > options_.contents->EndLine()) {
      break;
    }

    std::list<ColumnRange> line_breaks = ComputeBreaks(line);
    if (line == options_.begin.line) {
      while (!line_breaks.empty() &&
             line_breaks.front().end <= options_.begin.column &&
             !line_breaks.front().end.IsZero())
        line_breaks.pop_front();
    }
    while (LineNumberDelta(output.size()) < options_.lines_shown &&
           !line_breaks.empty()) {
      auto columns = line_breaks.front();
      output.push_back(
          Range::InLine(line, columns.begin, columns.end - columns.begin));
      line_breaks.pop_front();

      if ((!line_breaks.empty() || line < options_.contents->EndLine()) &&
          options_.margin_lines <= options_.lines_shown / 2 &&
          LineNumberDelta(output.size()) == options_.lines_shown &&
          FindPositionInScreen(output, options_.active_position) >=
              LineNumber() + options_.lines_shown - options_.margin_lines) {
        output.pop_front();
      }
    }
  }
  CHECK_LE(LineNumberDelta(output.size()), options_.lines_shown);

  if (!output.empty()) output = AdjustToHonorMargin(std::move(output));

  while (LineNumberDelta(output.size()) < options_.lines_shown) {
    output.push_back(
        Range(LineColumn(options_.contents->EndLine() + LineNumberDelta(1)),
              std::numeric_limits<LineColumn>::max()));
  }

  return output;
}

void LineScrollControl::SignalReaderDone() {
  if (++readers_done_ < readers_.size()) {
    VLOG(8) << "Readers done: " << readers_done_ << " out of "
            << readers_.size();
    return;
  }
  readers_done_ = 0;
  VLOG(6) << "Advancing, finished range: " << range();
  if (ranges_.size() > 1) ranges_.pop_front();
  VLOG(7) << "Next range: " << range();

  for (auto& c : readers_) {
    c->state_ = Reader::State::kProcessing;
  }
}

Range LineScrollControl::range() const {
  return ranges_.empty()
             ? Range::InLine(options_.contents->EndLine() + LineNumberDelta(1),
                             ColumnNumber(),
                             std::numeric_limits<ColumnNumberDelta>::max())
             : ranges_.front();
}

Range LineScrollControl::next_range() const {
  return ranges_.size() < 2
             ? Range::InLine(options_.contents->EndLine(),
                             options_.contents->at(options_.contents->EndLine())
                                 ->EndColumn(),
                             std::numeric_limits<ColumnNumberDelta>::max())
             : *(std::next(ranges_.begin()));
}

bool LineScrollControl::CurrentRangeContainsPosition(
    LineColumn position) const {
  position.line = min(position.line, options_.contents->EndLine());
  position.column =
      min(position.column, options_.contents->at(position.line)->EndColumn());
  if (range().begin.line == options_.contents->EndLine()) {
    return position >= range().begin;
  }
  return Range(range().begin, next_range().begin).Contains(position);
}

}  // namespace afc::editor
