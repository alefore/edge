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

LineScrollControl::Reader::Data LineScrollControl::Reader::Read() {
  std::optional<Range> range;
  switch (state_) {
    case State::kDone:
      break;
    case State::kProcessing:
      range = parent_->range();
  }

  Data data{.range = range,
            .has_active_cursor = parent_->CurrentRangeContainsPosition(
                parent_->options_.active_position),
            .current_cursors = {}};

  if (state_ == State::kProcessing) {
    LineNumber line = range->begin.line;
    auto it = parent_->cursors_.find(line);
    if (it != parent_->cursors_.end()) {
      for (auto& column : it->second) {
        if (parent_->CurrentRangeContainsPosition(LineColumn(line, column)))
          data.current_cursors.insert(column);
      }
    }
  }

  state_ = State::kDone;
  parent_->SignalReaderDone();
  return data;
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
      return r.end > output.front().begin.column ||
             r.begin >= output.front().begin.column;
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
             !line_breaks.front().end.IsZero()) {
        line_breaks.pop_front();
      }
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
    output.push_back(Range::InLine(
        LineNumber(options_.contents->EndLine() + LineNumberDelta(1)),
        ColumnNumber(0), ColumnNumberDelta(0)));
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

namespace {
const bool line_scroll_control_tests_registration =
    tests::Register(L"LineScrollControl", [] {
      auto get_ranges = [](LineScrollControl::Options options) {
        std::vector<Range> output;
        auto reader = LineScrollControl::New(options)->NewReader();
        for (LineNumberDelta i; i < options.lines_shown; ++i) {
          output.push_back(*reader->Read().range);
        }
        return output;
      };
      auto get_active_cursors = [](LineScrollControl::Options options) {
        std::vector<LineNumber> output;
        auto reader = LineScrollControl::New(options)->NewReader();
        for (LineNumber i; i.ToDelta() < options.lines_shown; ++i) {
          if (reader->Read().has_active_cursor) {
            output.push_back(i);
            VLOG(3) << "Found active cursor at line: " << i;
          }
        }
        return output;
      };
      auto new_test = [](std::wstring name, auto callback) {
        return tests::Test(
            {.name = name, .callback = [callback] {
               auto contents = std::make_shared<BufferContents>();
               contents->AppendToLine(LineNumber(), Line(L"0alejandro"));
               for (const auto& s : std::list<std::wstring>{
                        L"1forero",
                        L"2cuervo",
                        L"",
                        L"4blah",
                        L"",
                        L"6something or other",
                        L"7something or other",
                        L"8something or other",
                        L"9something or other",
                        L"",
                        L"11foo",
                        L"12bar",
                        L"13quux",
                        L"",
                        L"15dog",
                        L"16lynx",
                    })
                 contents->push_back(s);
               static CursorsSet active_cursors;
               LineScrollControl::Options options{
                   .contents = contents,
                   .active_position = LineColumn(),
                   .active_cursors = new CursorsSet(),  // &active_cursors,
                   .line_wrap_style = LineWrapStyle::kBreakWords,
                   .symbol_characters = L"abcdefghijklmnopqrstuvwxyz",
                   .lines_shown = LineNumberDelta(10),
                   .columns_shown = ColumnNumberDelta(80),
                   .begin = {},
                   .margin_lines = LineNumberDelta(2)};

               callback(options);
             }});
      };
      return std::vector<tests::Test>(
          {new_test(L"Construction",
                    [](auto options) {
                      LineScrollControl::New(options)->NewReader();
                    }),
           new_test(L"TopMargin",
                    [&](auto options) {
                      options.active_position =
                          LineColumn(LineNumber(4), ColumnNumber(3));
                      options.begin = LineColumn(LineNumber(7));
                      CHECK_EQ(get_ranges(options)[0],
                               Range::InLine(
                                   LineNumber(2), ColumnNumber(0),
                                   ColumnNumberDelta(sizeof("2cuervo") - 1)));
                      CHECK(get_active_cursors(options) ==
                            std::vector<LineNumber>({LineNumber(2)}));
                    }),
           new_test(L"IgnoreLargeMargins",
                    [&](auto options) {
                      options.margin_lines = LineNumberDelta(6);
                      options.active_position =
                          LineColumn(LineNumber(4), ColumnNumber(3));
                      options.begin = LineColumn(LineNumber(7));
                      CHECK_EQ(get_ranges(options)[0],
                               Range::InLine(
                                   LineNumber(4), ColumnNumber(0),
                                   ColumnNumberDelta(sizeof("4blah") - 1)));
                      CHECK(get_active_cursors(options) ==
                            std::vector<LineNumber>({LineNumber(0)}));
                    }),
           new_test(L"TopMarginForceScrollToBegin",
                    [&](auto options) {
                      options.active_position =
                          LineColumn(LineNumber(2), ColumnNumber(3));
                      options.margin_lines = LineNumberDelta(4);
                      options.begin = LineColumn(LineNumber(7));
                      CHECK_EQ(get_ranges(options)[0],
                               Range::InLine(LineNumber(0), ColumnNumber(0),
                                             ColumnNumberDelta(
                                                 sizeof("0alejandro") - 1)));
                      CHECK(get_active_cursors(options) ==
                            std::vector<LineNumber>({LineNumber(2)}));
                    }),
           new_test(L"BottomMarginForceScroll",
                    [&](auto options) {
                      options.active_position =
                          LineColumn(LineNumber(11), ColumnNumber(3));
                      options.begin = LineColumn(LineNumber(2));
                      CHECK_EQ(LineNumber(11) + options.margin_lines -
                                   (options.lines_shown - LineNumberDelta(1)),
                               LineNumber(4));
                      CHECK_EQ(get_ranges(options)[0],
                               Range::InLine(
                                   LineNumber(4), ColumnNumber(0),
                                   ColumnNumberDelta(sizeof("4blah") - 1)));
                      CHECK_EQ(LineNumber(11) - LineNumber(4),
                               LineNumberDelta(7));
                      CHECK(get_active_cursors(options) ==
                            std::vector<LineNumber>({LineNumber(7)}));
                    }),
           new_test(
               L"BottomMarginForceScrollToBottom",
               [&](auto options) {
                 options.active_position =
                     LineColumn(LineNumber(14), ColumnNumber(3));
                 options.margin_lines = LineNumberDelta(5);
                 options.begin = LineColumn(LineNumber(3));
                 CHECK_EQ(LineNumber(16) -
                              (options.lines_shown - LineNumberDelta(1)),
                          LineNumber(7));
                 CHECK_EQ(
                     get_ranges(options)[0],
                     Range::InLine(
                         LineNumber(7), ColumnNumber(),
                         ColumnNumberDelta(sizeof("7something or other") - 1)));
                 CHECK_EQ(LineNumber(14) - LineNumber(7), LineNumberDelta(7));
                 CHECK(get_active_cursors(options) ==
                       std::vector<LineNumber>({LineNumber(7)}));
               }),
           new_test(L"TopMarginWithLineWraps",
                    [&](auto options) {
                      options.begin = LineColumn(LineNumber(11));
                      options.columns_shown = ColumnNumberDelta(2);
                      options.active_position =
                          LineColumn(LineNumber(2), ColumnNumber(5));
                      options.margin_lines = LineNumberDelta(4);
                      auto ranges = get_ranges(options);
                      // Margins:
                      CHECK_EQ(ranges[0],
                               Range::InLine(LineNumber(1), ColumnNumber(4),
                                             ColumnNumberDelta(2)));
                      CHECK_EQ(ranges[1],
                               Range::InLine(LineNumber(1), ColumnNumber(6),
                                             ColumnNumberDelta(1)));
                      CHECK_EQ(ranges[2],
                               Range::InLine(LineNumber(2), ColumnNumber(0),
                                             ColumnNumberDelta(2)));
                      CHECK_EQ(ranges[3],
                               Range::InLine(LineNumber(2), ColumnNumber(2),
                                             ColumnNumberDelta(2)));
                      // Actual cursor:
                      CHECK_EQ(ranges[4],
                               Range::InLine(LineNumber(2), ColumnNumber(4),
                                             ColumnNumberDelta(2)));
                      // Next line:
                      CHECK_EQ(ranges[5],
                               Range::InLine(LineNumber(2), ColumnNumber(6),
                                             ColumnNumberDelta(1)));
                      CHECK(get_active_cursors(options) ==
                            std::vector<LineNumber>({LineNumber(4)}));
                    }),
           new_test(L"TopMarginWithLineWrapsForceScrollToTop",
                    [&](auto options) {
                      options.active_position =
                          LineColumn(LineNumber(1), ColumnNumber(5));
                      options.margin_lines = LineNumberDelta(20);
                      options.columns_shown = ColumnNumberDelta(2);
                      options.lines_shown = LineNumberDelta(50);
                      CHECK_EQ(get_ranges(options)[0],
                               Range::InLine(LineNumber(0), ColumnNumber(0),
                                             ColumnNumberDelta(2)));
                      CHECK(get_active_cursors(options) ==
                            std::vector<LineNumber>({LineNumber(7)}));
                    }),
           new_test(L"BottomMarginWithLineWrapsForceScrollToBottom",
                    [&](auto options) {
                      options.active_position =
                          LineColumn(LineNumber(15), ColumnNumber(3));
                      options.margin_lines = LineNumberDelta(20);
                      options.columns_shown = ColumnNumberDelta(2);
                      options.lines_shown = LineNumberDelta(50);
                      auto ranges = get_ranges(options);
                      CHECK_EQ(ranges[49],
                               Range::InLine(LineNumber(16), ColumnNumber(4),
                                             ColumnNumberDelta(2)));
                      CHECK_EQ(ranges[48],
                               Range::InLine(LineNumber(16), ColumnNumber(2),
                                             ColumnNumberDelta(2)));
                      CHECK_EQ(ranges[47],
                               Range::InLine(LineNumber(16), ColumnNumber(0),
                                             ColumnNumberDelta(2)));
                      CHECK_EQ(ranges[46],
                               Range::InLine(LineNumber(15), ColumnNumber(4),
                                             ColumnNumberDelta(1)));
                      CHECK(get_active_cursors(options) ==
                            std::vector<LineNumber>({LineNumber(45)}));
                    }),
           new_test(L"EverythingFits",
                    [&](auto options) {
                      options.active_position =
                          LineColumn(LineNumber(10), ColumnNumber(12));
                      options.margin_lines = LineNumberDelta(20);
                      options.lines_shown = LineNumberDelta(500);
                      auto ranges = get_ranges(options);
                      CHECK_EQ(ranges[0],
                               Range::InLine(LineNumber(0), ColumnNumber(0),
                                             ColumnNumberDelta(
                                                 sizeof("0alejandro") - 1)));
                      CHECK_EQ(ranges[16],
                               Range::InLine(
                                   LineNumber(16), ColumnNumber(0),
                                   ColumnNumberDelta(sizeof("16lynx") - 1)));
                      CHECK_EQ(ranges[17],
                               Range::InLine(LineNumber(17), ColumnNumber(0),
                                             ColumnNumberDelta(0)));
                      CHECK_EQ(ranges[18], ranges[17]);
                      CHECK(get_active_cursors(options) ==
                            std::vector<LineNumber>({LineNumber(10)}));
                    }),
           new_test(L"Cursors", [&](auto options) {
             CursorsSet cursors;
             options.active_cursors = &cursors;
             LineScrollControl::New(options)->NewReader();
           })});
    }());
}  // namespace
}  // namespace afc::editor
