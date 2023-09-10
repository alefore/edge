#include "src/buffer_contents_view_layout.h"

#include <glog/logging.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <iostream>

#include "src/buffer_output_producer.h"
#include "src/buffer_variables.h"
#include "src/buffer_widget.h"
#include "src/columns_vector.h"
#include "src/language/text/line_sequence.h"
#include "src/language/wstring.h"
#include "src/line_output.h"
#include "src/tests/tests.h"
#include "src/widget.h"

namespace afc::editor {
namespace {
using infrastructure::screen::CursorsSet;
using language::lazy_string::ColumnNumber;
using language::lazy_string::ColumnNumberDelta;
using language::text::Line;
using language::text::LineColumn;
using language::text::LineNumber;
using language::text::LineNumberDelta;
using language::text::MutableLineSequence;
using language::text::Range;

std::list<ColumnRange> ComputeBreaks(
    const BufferContentsViewLayout::Input& input, LineNumber line) {
  return BreakLineForOutput(input.contents.at(line).value(),
                            input.columns_shown, input.line_wrap_style,
                            input.symbol_characters);
}

// If the position is before the ranges, returns 0. If the position is after
// the ranges, returns the last line.
LineNumber FindPositionInScreen(
    const std::vector<BufferContentsViewLayout::Line>& screen_lines,
    LineColumn position) {
  CHECK(!screen_lines.empty());
  if (position < screen_lines.front().range.begin) {
    return LineNumber();
  }

  if (screen_lines.back().range.end < position) {
    return LineNumber(screen_lines.size()) -
           LineNumberDelta(1);  // Optimization.
  }

  LineNumber output;
  for (auto it = std::next(screen_lines.cbegin()); it != screen_lines.cend();
       ++it) {
    if (position < it->range.begin) {
      return output;
    }
    ++output;
  }
  CHECK_EQ(LineNumber(screen_lines.size()) - LineNumberDelta(1), output);
  return output;
}

const bool find_position_in_screen_tests_registration = tests::Register(
    L"FindPositionInScreen",
    {{.name = L"BeforeFirst",
      .callback =
          [] {
            CHECK_EQ(
                FindPositionInScreen(
                    std::vector<BufferContentsViewLayout::Line>({
                        {.range =
                             Range::InLine(LineNumber(10), ColumnNumber(20),
                                           ColumnNumberDelta(8)),
                         .has_active_cursor = false,
                         .current_cursors = {}},
                        {.range = Range::InLine(LineNumber(11), ColumnNumber(0),
                                                ColumnNumberDelta(10)),
                         .has_active_cursor = false,
                         .current_cursors = {}},
                    }),
                    LineColumn(LineNumber(4), ColumnNumber(25))),
                LineNumber());
          }},
     {.name = L"InFirst",
      .callback =
          [] {
            CHECK_EQ(FindPositionInScreen(
                         std::vector<BufferContentsViewLayout::Line>(
                             {{.range = Range::InLine(LineNumber(10),
                                                      ColumnNumber(20),
                                                      ColumnNumberDelta(8)),
                               .has_active_cursor = false,
                               .current_cursors = {}}}),
                         LineColumn(LineNumber(10), ColumnNumber(25))),
                     LineNumber(0));
          }},
     {.name = L"BeforeSecond",
      .callback =
          [] {
            CHECK_EQ(FindPositionInScreen(
                         std::vector<BufferContentsViewLayout::Line>(
                             {{.range = Range::InLine(LineNumber(10),
                                                      ColumnNumber(20),
                                                      ColumnNumberDelta(8)),
                               .has_active_cursor = false,
                               .current_cursors = {}},
                              {.range = Range::InLine(LineNumber(11),
                                                      ColumnNumber(0),
                                                      ColumnNumberDelta(10)),
                               .has_active_cursor = false,
                               .current_cursors = {}}}),
                         LineColumn(LineNumber(10), ColumnNumber(95))),
                     LineNumber(0));
          }},
     {.name = L"InSecond",
      .callback =
          [] {
            CHECK_EQ(
                FindPositionInScreen(
                    std::vector<BufferContentsViewLayout::Line>({
                        {.range =
                             Range::InLine(LineNumber(10), ColumnNumber(20),
                                           ColumnNumberDelta(8)),
                         .has_active_cursor = false,
                         .current_cursors = {}},
                        {.range = Range::InLine(LineNumber(11), ColumnNumber(0),
                                                ColumnNumberDelta(10)),
                         .has_active_cursor = false,
                         .current_cursors = {}},
                    }),
                    LineColumn(LineNumber(11), ColumnNumber(2))),
                LineNumber(1));
          }},
     {.name = L"AfterLast", .callback = [] {
        CHECK_EQ(
            FindPositionInScreen(
                std::vector<BufferContentsViewLayout::Line>({
                    {.range = Range::InLine(LineNumber(10), ColumnNumber(20),
                                            ColumnNumberDelta(8)),
                     .has_active_cursor = false,
                     .current_cursors = {}},
                    {.range = Range::InLine(LineNumber(11), ColumnNumber(0),
                                            ColumnNumberDelta(10)),
                     .has_active_cursor = false,
                     .current_cursors = {}},
                }),
                LineColumn(LineNumber(12))),
            LineNumber(1));
      }}});

BufferContentsViewLayout::Line GetScreenLine(
    const MutableLineSequence& contents, std::optional<LineColumn> active_position,
    const std::map<LineNumber, std::set<ColumnNumber>>& cursors,
    LineNumber line, ColumnRange column_range) {
  CHECK_LE(line, contents.EndLine());
  Range range =
      Range(LineColumn(line, column_range.begin),
            LineColumn(line, column_range.end < contents.at(line)->EndColumn()
                                 ? column_range.end
                                 : std::numeric_limits<ColumnNumber>::max()));

  auto contains_cursor = [&](ColumnNumber column) {
    return range.Contains(LineColumn(line, column));
  };

  BufferContentsViewLayout::Line output{
      .range = range,
      .has_active_cursor = active_position.has_value() &&
                           line == active_position->line &&
                           contains_cursor(active_position->column),
      .current_cursors = {}};

  if (auto cursors_it = cursors.find(line); cursors_it != cursors.end()) {
    for (auto& column : cursors_it->second) {
      if (contains_cursor(column)) output.current_cursors.insert(column);
    }
  }

  return output;
}

const bool get_screen_line_tests_registration = tests::Register(
    L"GetScreenLine",
    {{.name = L"SimpleLine", .callback = [] {
        MutableLineSequence contents;
        contents.AppendToLine(LineNumber(0), Line(L"foo"));
        BufferContentsViewLayout::Line output =
            GetScreenLine(contents, std::nullopt, {}, LineNumber(0),
                          ColumnRange{ColumnNumber(0), ColumnNumber(3)});
        CHECK_EQ(output.range.end,
                 LineColumn(LineNumber(0),
                            std::numeric_limits<ColumnNumber>::max()));
      }}});

std::vector<BufferContentsViewLayout::Line> PrependLines(
    const BufferContentsViewLayout::Input& options,
    const std::map<LineNumber, std::set<ColumnNumber>>& cursors,
    LineNumber line, LineNumberDelta lines_desired,
    std::vector<BufferContentsViewLayout::Line> output) {
  std::list<ColumnRange> line_breaks = ComputeBreaks(options, line);
  if (line == output.front().range.begin.line) {
    line_breaks.remove_if([&](const ColumnRange& r) {
      return r.end > output.front().range.begin.column ||
             r.begin >= output.front().range.begin.column;
    });
  }
  std::vector<BufferContentsViewLayout::Line> lines_to_insert;
  for (auto& r : line_breaks) {
    lines_to_insert.push_back(GetScreenLine(
        options.contents, options.active_position, cursors, line, r));
  }
  auto insert_start = lines_to_insert.begin();
  if (LineNumberDelta(lines_to_insert.size()) > lines_desired) {
    insert_start += lines_to_insert.size() - lines_desired.read();
  }
  output.insert(output.begin(), insert_start, lines_to_insert.end());
  return output;
}

std::vector<BufferContentsViewLayout::Line> AdjustToHonorMargin(
    const BufferContentsViewLayout::Input& options,
    const std::map<LineNumber, std::set<ColumnNumber>>& cursors,
    std::vector<BufferContentsViewLayout::Line> output) {
  if (output.empty() || options.margin_lines > options.lines_shown / 2 ||
      options.begin == LineColumn()) {
    return output;
  }

  std::optional<LineNumber> position_line =
      options.active_position.has_value()
          ? FindPositionInScreen(output, *options.active_position)
          : std::optional<LineNumber>();
  auto lines_desired = [&] {
    return std::max(std::max(LineNumberDelta(0),
                             options.margin_lines - position_line->ToDelta()),
                    options.lines_shown - LineNumberDelta(output.size()));
  };
  for (LineNumber line = options.begin.column.IsZero()
                             ? options.begin.line - LineNumberDelta(1)
                             : options.begin.line;
       position_line.has_value() && lines_desired() > LineNumberDelta();
       --line) {
    LineNumberDelta original_length(output.size());
    auto lines_to_insert = lines_desired();
    output = PrependLines(options, cursors, line, lines_to_insert,
                          std::move(output));
    CHECK_GE(LineNumberDelta(output.size()), original_length);
    position_line.value() += LineNumberDelta(output.size()) - original_length;
    if (line.IsZero()) break;
  }
  return output;
}

std::optional<LineNumberDelta> GetCursorIndex(
    const BufferContentsViewLayout& window) {
  LineNumberDelta i;
  for (const BufferContentsViewLayout::Line& screen_line : window.lines) {
    if (screen_line.has_active_cursor) return i;
    ++i;
  }
  return std::nullopt;
}
}  // namespace

/* static */
BufferContentsViewLayout BufferContentsViewLayout::Get(
    BufferContentsViewLayout::Input options) {
  CHECK_GE(options.lines_shown, LineNumberDelta());
  CHECK_GE(options.status_lines, LineNumberDelta());
  CHECK_LE(options.status_lines, options.lines_shown);
  if (options.active_position.has_value()) {
    options.active_position->line =
        std::min(options.active_position->line, options.contents.EndLine());
    options.active_position->column = std::min(
        options.active_position->column,
        options.contents.at(options.active_position->line)->EndColumn());
    options.begin =
        std::max(std::min(options.begin, *options.active_position),
                 LineColumn(options.active_position->line.MinusHandlingOverflow(
                     options.lines_shown - options.status_lines)));
  }
  std::map<LineNumber, std::set<ColumnNumber>> cursors;
  for (auto& cursor : options.active_cursors) {
    cursors[cursor.line].insert(cursor.column);
  }

  DVLOG(4) << "Initial line: " << options.begin.line;
  BufferContentsViewLayout output;
  for (LineNumber line = options.begin.line;
       LineNumberDelta(output.lines.size()) < options.lines_shown; ++line) {
    if (line > options.contents.EndLine()) {
      break;
    }

    std::list<ColumnRange> line_breaks = ComputeBreaks(options, line);
    if (line == options.begin.line) {
      while (!line_breaks.empty() &&
             line_breaks.front().end <= options.begin.column &&
             !line_breaks.front().end.IsZero()) {
        line_breaks.pop_front();
      }
    }
    while (LineNumberDelta(output.lines.size()) < options.lines_shown &&
           !line_breaks.empty()) {
      output.lines.push_back(GetScreenLine(options.contents,
                                           options.active_position, cursors,
                                           line, line_breaks.front()));
      line_breaks.pop_front();
      DVLOG(5) << "Added screen line for line: " << line
               << ", range: " << output.lines.back().range;

      if ((!line_breaks.empty() || line < options.contents.EndLine()) &&
          options.margin_lines <= options.lines_shown / 2 &&
          LineNumberDelta(output.lines.size()) == options.lines_shown &&
          (options.active_position.has_value() &&
           FindPositionInScreen(output.lines, *options.active_position) >=
               LineNumber() + options.lines_shown - options.margin_lines)) {
        // TODO: This is slow? Maybe we can do all deletions at once, outside of
        // the loop? Didn't really look into the code to see if this is
        // feasible.
        CHECK(!output.lines.empty());
        output.lines.erase(output.lines.begin());
      }
    }
  }
  CHECK_LE(LineNumberDelta(output.lines.size()), options.lines_shown);

  output.lines = AdjustToHonorMargin(options, cursors, std::move(output.lines));

  // Initialize output.status_position:
  LineNumberDelta lines_to_drop = std::max(
      LineNumberDelta(), LineNumberDelta(output.lines.size()) +
                             options.status_lines - options.lines_shown);

  VLOG(5) << "Wrapping up: lines_shown: " << options.lines_shown
          << ", status_lines: " << options.status_lines
          << ", output.lines.size: " << output.lines.size();
  LineNumberDelta cursor_index =
      GetCursorIndex(output).value_or(LineNumberDelta());
  if (options.lines_shown <
          LineNumberDelta(output.lines.size()) + options.status_lines &&
      cursor_index >= options.lines_shown - options.status_lines -
                          std::max(options.margin_lines, LineNumberDelta(1))) {
    output.lines.erase(
        output.lines.begin(),
        output.lines.begin() +
            std::min(lines_to_drop,
                     LineNumberDelta(1) + cursor_index -
                         (options.lines_shown - options.status_lines -
                          std::max(options.margin_lines, LineNumberDelta(1))))
                .read());
    output.view_start =
        output.lines.empty() ? options.begin : output.lines.front().range.begin;
  } else if (LineNumberDelta(output.lines.size()) <= lines_to_drop) {
    output.lines.clear();
  } else {
    output.lines.resize(output.lines.size() - lines_to_drop.read());
    output.view_start =
        output.lines.empty() ? options.begin : output.lines.front().range.begin;
  }
  return output;
}

namespace {
const bool buffer_contents_view_layout_tests_registration =
    tests::Register(L"BufferContentsViewLayout", [] {
      auto get_ranges = [](BufferContentsViewLayout::Input options) {
        std::vector<Range> output;
        for (const auto& l : BufferContentsViewLayout::Get(options).lines) {
          output.push_back(l.range);
          DVLOG(5) << "Range for testing: " << output.back();
        }
        return output;
      };
      auto get_active_cursors = [](BufferContentsViewLayout::Input options) {
        std::vector<LineNumber> output;
        LineNumber i;
        for (const auto& l : BufferContentsViewLayout::Get(options).lines) {
          if (l.has_active_cursor) {
            output.push_back(i);
          }
          ++i;
        }
        return output;
      };
      auto new_test = [](std::wstring name, auto callback) {
        return tests::Test(
            {.name = name, .callback = [callback]() {
               auto contents = std::make_shared<MutableLineSequence>();
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
               BufferContentsViewLayout::Input options{
                   .contents = *contents,
                   .active_position = LineColumn(),
                   .active_cursors = active_cursors,
                   .line_wrap_style = LineWrapStyle::kBreakWords,
                   .symbol_characters = L"abcdefghijklmnopqrstuvwxyz",
                   .lines_shown = LineNumberDelta(10),
                   .status_lines = LineNumberDelta(),
                   .columns_shown = ColumnNumberDelta(80),
                   .begin = {},
                   .margin_lines = LineNumberDelta(2)};

               callback(options);
             }});
      };
      auto RangeToLineEnd = [](LineColumn p) {
        return Range(
            p, LineColumn(p.line, std::numeric_limits<ColumnNumber>::max()));
      };

      return std::vector<tests::Test>(
          {new_test(
               L"Construction",
               [](auto options) { BufferContentsViewLayout::Get(options); }),
           new_test(L"TopMargin",
                    [&](auto options) {
                      options.active_position =
                          LineColumn(LineNumber(4), ColumnNumber(3));
                      options.begin = LineColumn(LineNumber(7));
                      CHECK_EQ(get_ranges(options)[0],
                               RangeToLineEnd(LineColumn(LineNumber(2))));
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
                               RangeToLineEnd(LineColumn(LineNumber(4))));
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
                               RangeToLineEnd(LineColumn(LineNumber(0))));
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
                               RangeToLineEnd(LineColumn(LineNumber(4))));
                      CHECK_EQ(LineNumber(11) - LineNumber(4),
                               LineNumberDelta(7));
                      CHECK(get_active_cursors(options) ==
                            std::vector<LineNumber>({LineNumber(7)}));
                    }),
           new_test(L"BottomMarginForceScrollToBottom",
                    [&](auto options) {
                      options.active_position =
                          LineColumn(LineNumber(14), ColumnNumber(3));
                      options.margin_lines = LineNumberDelta(5);
                      options.begin = LineColumn(LineNumber(3));
                      CHECK_EQ(LineNumber(16) -
                                   (options.lines_shown - LineNumberDelta(1)),
                               LineNumber(7));
                      CHECK_EQ(get_ranges(options)[0],
                               RangeToLineEnd(LineColumn(LineNumber(7))));
                      CHECK_EQ(LineNumber(14) - LineNumber(7),
                               LineNumberDelta(7));
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
                      CHECK_EQ(ranges[1], RangeToLineEnd(LineColumn(
                                              LineNumber(1), ColumnNumber(6))));
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
                      CHECK_EQ(ranges[5], RangeToLineEnd(LineColumn(
                                              LineNumber(2), ColumnNumber(6))));
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
           new_test(
               L"BottomMarginWithLineWrapsForceScrollToBottom",
               [&](auto options) {
                 options.active_position =
                     LineColumn(LineNumber(15), ColumnNumber(3));
                 options.margin_lines = LineNumberDelta(20);
                 options.columns_shown = ColumnNumberDelta(2);
                 options.lines_shown = LineNumberDelta(50);
                 auto ranges = get_ranges(options);
                 CHECK_EQ(ranges[49], RangeToLineEnd(LineColumn(
                                          LineNumber(16), ColumnNumber(4))));
                 CHECK_EQ(ranges[48],
                          Range::InLine(LineNumber(16), ColumnNumber(2),
                                        ColumnNumberDelta(2)));
                 CHECK_EQ(ranges[47],
                          Range::InLine(LineNumber(16), ColumnNumber(0),
                                        ColumnNumberDelta(2)));
                 CHECK_EQ(ranges[46], RangeToLineEnd(LineColumn(
                                          LineNumber(15), ColumnNumber(4))));
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
                      CHECK_EQ(ranges.size(), size_t(17));
                      CHECK_EQ(ranges[0],
                               RangeToLineEnd(LineColumn(LineNumber(0))));
                      CHECK_EQ(ranges[16],
                               RangeToLineEnd(LineColumn(LineNumber(16))));
                      CHECK(get_active_cursors(options) ==
                            std::vector<LineNumber>({LineNumber(10)}));
                    }),
           new_test(L"NoActivePosition",
                    [&](auto options) {
                      options.active_position = std::nullopt;
                      options.begin = LineColumn(LineNumber(3));
                      options.lines_shown = LineNumberDelta(5);
                      auto ranges = get_ranges(options);
                      CHECK_EQ(ranges.size(), size_t(5));
                      CHECK_EQ(ranges[0],
                               RangeToLineEnd(LineColumn(LineNumber(3))));
                      CHECK(get_active_cursors(options) ==
                            std::vector<LineNumber>());
                    }),
           new_test(L"StatusEatsFromEmpty",
                    [&](auto options) {
                      options.lines_shown = LineNumberDelta(20);
                      options.status_lines = LineNumberDelta(5);
                      auto ranges = get_ranges(options);
                      CHECK_EQ(ranges.size(), size_t(15));
                      CHECK_EQ(ranges[0], RangeToLineEnd(LineColumn()));
                      CHECK_EQ(ranges[14],
                               RangeToLineEnd(LineColumn(LineNumber(14))));
                    }),
           new_test(L"StatusEatsFromEmptyAtBottom",
                    [&](auto options) {
                      options.active_position =
                          LineColumn(LineNumber(15), ColumnNumber(12));
                      options.lines_shown = LineNumberDelta(20);
                      options.status_lines = LineNumberDelta(5);
                      auto ranges = get_ranges(options);
                      CHECK_EQ(ranges.size(), size_t(15));
                      CHECK_EQ(ranges[0],
                               RangeToLineEnd(LineColumn(LineNumber(2))));
                      CHECK_EQ(ranges[14],
                               RangeToLineEnd(LineColumn(LineNumber(16))));
                    }),
           new_test(L"CursorWhenPositionAtEndFits",
                    [&](auto options) {
                      options.status_lines = LineNumberDelta();
                      options.lines_shown = LineNumberDelta(10);
                      options.active_position = options.contents.range().end;
                      auto output = BufferContentsViewLayout::Get(options);
                      CHECK_EQ(output.lines.size(), 10ul);
                      CHECK_EQ(output.lines.back().range,
                               RangeToLineEnd(LineColumn(LineNumber(16))));
                      CHECK(output.lines.back().has_active_cursor);
                    }),
           new_test(L"CursorWhenPositionAtEndDrops",
                    [&](auto options) {
                      options.status_lines = LineNumberDelta(1);
                      options.lines_shown = LineNumberDelta(10);
                      options.active_position = options.contents.range().end;
                      auto output = BufferContentsViewLayout::Get(options);
                      CHECK_EQ(output.lines.size(), 9ul);
                      CHECK_EQ(output.lines.back().range,
                               RangeToLineEnd(LineColumn(LineNumber(16))));
                      CHECK(output.lines.back().has_active_cursor);
                    }),
           new_test(L"CursorWhenPositionPastEndFits",
                    [&](auto options) {
                      options.status_lines = LineNumberDelta();
                      options.lines_shown = LineNumberDelta(10);
                      options.active_position = LineColumn(LineNumber(9999));
                      auto output = BufferContentsViewLayout::Get(options);
                      CHECK_EQ(output.lines.size(), 10ul);
                      CHECK_EQ(output.lines.back().range,
                               RangeToLineEnd(LineColumn(LineNumber(16))));
                      CHECK(output.lines.back().has_active_cursor);
                    }),
           new_test(L"CursorWhenPositionPastEndDrops",
                    [&](auto options) {
                      options.status_lines = LineNumberDelta(2);
                      options.lines_shown = LineNumberDelta(10);
                      options.active_position = LineColumn(LineNumber(9999));
                      auto output = BufferContentsViewLayout::Get(options);
                      CHECK_EQ(output.lines.size(), 8ul);
                      CHECK_EQ(output.lines.back().range,
                               RangeToLineEnd(LineColumn(LineNumber(16))));
                      CHECK(output.lines.back().has_active_cursor);
                    }),
           new_test(L"ViewStartWithPositionAtEnd",
                    [&](auto options) {
                      options.active_position = LineColumn(
                          LineNumber(16), ColumnNumber(sizeof("16lynx") - 1));
                      options.status_lines = LineNumberDelta(1);
                      options.lines_shown = LineNumberDelta(3);
                      options.margin_lines = LineNumberDelta(2);
                      auto output = BufferContentsViewLayout::Get(options);
                      CHECK_EQ(output.lines.size(), 2ul);
                      CHECK_EQ(output.lines.front().range,
                               RangeToLineEnd(LineColumn(LineNumber(15))));
                      CHECK_EQ(output.view_start, LineColumn(LineNumber(15)));
                    }),
           new_test(L"StatusDownWhenFits",
                    [&](auto options) {
                      options.active_position = LineColumn(LineNumber(16));
                      options.status_lines = LineNumberDelta(10);
                      options.lines_shown = LineNumberDelta(27);
                      auto output = BufferContentsViewLayout::Get(options);
                      CHECK_EQ(output.lines.size(), 17ul);
                    }),
           new_test(L"ViewStartWithPositionAtEndShortColumns",
                    [&](auto options) {
                      options.active_position = LineColumn(
                          LineNumber(16), ColumnNumber(sizeof("16lynx") - 1));
                      options.status_lines = LineNumberDelta(1);
                      options.lines_shown = LineNumberDelta(3);
                      options.columns_shown = ColumnNumberDelta(3);
                      options.margin_lines = LineNumberDelta(2);
                      auto output = BufferContentsViewLayout::Get(options);
                      CHECK_EQ(output.lines.size(), 2ul);
    // TODO: This test fails. Fix the code and make it pass.
#if 0
                      CHECK_EQ(output.lines.front().range,
                               Range::InLine(LineNumber(16), ColumnNumber(),
                                             ColumnNumberDelta(2)));
                      CHECK_EQ(output.view_start,
                               LineColumn(LineNumber(15), ColumnNumber(4)));
#endif
                    })});
    }());
}  // namespace
}  // namespace afc::editor
namespace std {
using afc::language::compute_hash;
using afc::language::MakeHashableIteratorRange;

std::size_t hash<afc::editor::BufferContentsViewLayout::Line>::operator()(
    const afc::editor::BufferContentsViewLayout::Line& line) const {
  return compute_hash(line.range, line.has_active_cursor,
                      MakeHashableIteratorRange(line.current_cursors.begin(),
                                                line.current_cursors.end()));
}

std::size_t hash<afc::editor::BufferContentsViewLayout>::operator()(
    const afc::editor::BufferContentsViewLayout& window) const {
  using namespace afc::editor;
  return compute_hash(
      MakeHashableIteratorRange(window.lines.begin(), window.lines.end()));
}
}  // namespace std
