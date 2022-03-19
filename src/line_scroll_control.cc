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
namespace {
std::list<ColumnRange> ComputeBreaks(const BufferContentsWindow::Input& input,
                                     LineNumber line) {
  return BreakLineForOutput(*input.contents->at(line), input.columns_shown,
                            input.line_wrap_style, input.symbol_characters);
}

// If the position is before the ranges, returns 0. If the position is after
// the ranges, returns the last line.
LineNumber FindPositionInScreen(
    const std::list<BufferContentsWindow::Line>& screen_lines,
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

const bool screen_lines_to_position_tests_registration = tests::Register(
    L"FindPositionInScreen",
    {{.name = L"BeforeFirst",
      .callback =
          [] {
            CHECK_EQ(
                FindPositionInScreen(
                    std::list<BufferContentsWindow::Line>({
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
                         std::list<BufferContentsWindow::Line>(
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
                         std::list<BufferContentsWindow::Line>(
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
                    std::list<BufferContentsWindow::Line>({
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
                std::list<BufferContentsWindow::Line>({
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

BufferContentsWindow::Line GetScreenLine(
    const BufferContentsWindow::Input& options,
    const std::map<LineNumber, std::set<ColumnNumber>>& cursors,
    LineNumber line, ColumnRange column_range) {
  Range range = Range::InLine(line, column_range.begin,
                              column_range.end - column_range.begin);

  auto contains_cursor = [&](ColumnNumber column) {
    CHECK_LE(line, options.contents->EndLine());
    if (column < column_range.begin) return false;
    if (column < column_range.end) return true;
    return range.end.column == options.contents->at(line)->EndColumn();
  };

  BufferContentsWindow::Line output{
      .range = range,
      .has_active_cursor = options.active_position.has_value() &&
                           line == options.active_position->line &&
                           contains_cursor(options.active_position->column),
      .current_cursors = {}};

  if (auto cursors_it = cursors.find(line); cursors_it != cursors.end()) {
    for (auto& column : cursors_it->second) {
      if (contains_cursor(column)) output.current_cursors.insert(column);
    }
  }

  return output;
}

std::list<BufferContentsWindow::Line> PrependLines(
    const BufferContentsWindow::Input& options,
    const std::map<LineNumber, std::set<ColumnNumber>>& cursors,
    LineNumber line, LineNumberDelta lines_desired,
    std::list<BufferContentsWindow::Line> output) {
  std::list<ColumnRange> line_breaks = ComputeBreaks(options, line);
  if (line == output.front().range.begin.line) {
    line_breaks.remove_if([&](const ColumnRange& r) {
      return r.end > output.front().range.begin.column ||
             r.begin >= output.front().range.begin.column;
    });
  }
  std::list<BufferContentsWindow::Line> lines_to_insert;
  for (auto& r : line_breaks) {
    lines_to_insert.push_back(GetScreenLine(options, cursors, line, r));
  }
  while (LineNumberDelta(lines_to_insert.size()) > lines_desired) {
    lines_to_insert.pop_front();
  }
  output.insert(output.begin(), lines_to_insert.begin(), lines_to_insert.end());
  return output;
}

std::list<BufferContentsWindow::Line> AdjustToHonorMargin(
    const BufferContentsWindow::Input& options,
    const std::map<LineNumber, std::set<ColumnNumber>>& cursors,
    std::list<BufferContentsWindow::Line> output) {
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
}  // namespace

/* static */
BufferContentsWindow BufferContentsWindow::Get(
    BufferContentsWindow::Input options) {
  CHECK(options.contents != nullptr);
  if (options.active_position.has_value()) {
    options.begin =
        std::max(std::min(options.begin, *options.active_position),
                 LineColumn(options.active_position->line.MinusHandlingOverflow(
                     options.lines_shown)));
    options.active_position->line =
        min(options.active_position->line, options.contents->EndLine());
    options.active_position->column =
        min(options.active_position->column,
            options.contents->at(options.active_position->line)->EndColumn());
  }
  std::map<LineNumber, std::set<ColumnNumber>> cursors;
  for (auto& cursor : *options.active_cursors) {
    cursors[cursor.line].insert(cursor.column);
  }

  DVLOG(4) << "Initial line: " << options.begin.line;
  BufferContentsWindow output;
  for (LineNumber line = options.begin.line;
       LineNumberDelta(output.lines.size()) < options.lines_shown; ++line) {
    if (line > options.contents->EndLine()) {
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
      output.lines.push_back(
          GetScreenLine(options, cursors, line, line_breaks.front()));
      line_breaks.pop_front();
      DVLOG(5) << "Added screen line for line: " << line
               << ", range: " << output.lines.back().range;

      if ((!line_breaks.empty() || line < options.contents->EndLine()) &&
          options.margin_lines <= options.lines_shown / 2 &&
          LineNumberDelta(output.lines.size()) == options.lines_shown &&
          (options.active_position.has_value() &&
           FindPositionInScreen(output.lines, *options.active_position) >=
               LineNumber() + options.lines_shown - options.margin_lines)) {
        output.lines.pop_front();
      }
    }
  }
  CHECK_LE(LineNumberDelta(output.lines.size()), options.lines_shown);

  output.lines = AdjustToHonorMargin(options, cursors, std::move(output.lines));
  return output;
}

namespace {
const bool line_scroll_control_tests_registration =
    tests::Register(L"LineScrollControl", [] {
      auto get_ranges = [](BufferContentsWindow::Input options) {
        std::vector<Range> output;
        for (const auto& l : BufferContentsWindow::Get(options).lines) {
          output.push_back(l.range);
          DVLOG(5) << "Range for testing: " << output.back();
        }
        return output;
      };
      auto get_active_cursors = [](BufferContentsWindow::Input options) {
        std::vector<LineNumber> output;
        LineNumber i;
        for (const auto& l : BufferContentsWindow::Get(options).lines) {
          if (l.has_active_cursor) {
            output.push_back(i);
          }
          ++i;
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
               BufferContentsWindow::Input options{
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
                    [](auto options) { BufferContentsWindow::Get(options); }),
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
                      CHECK_EQ(ranges.size(), size_t(17));
                      CHECK(get_active_cursors(options) ==
                            std::vector<LineNumber>({LineNumber(10)}));
                    }),
           new_test(L"NoActivePosition",
                    [&](auto options) {
                      options.active_position = std::nullopt;
                      options.begin = LineColumn(LineNumber(3));
                      options.lines_shown = LineNumberDelta(5);
                      auto ranges = get_ranges(options);
                      CHECK_EQ(ranges[0],
                               Range::InLine(LineNumber(3), ColumnNumber(0),
                                             ColumnNumberDelta()));
                      CHECK_EQ(ranges.size(), size_t(5));
                      CHECK(get_active_cursors(options) ==
                            std::vector<LineNumber>());
                    }),
           new_test(L"Cursors", [&](auto options) {
             CursorsSet cursors;
             options.active_cursors = &cursors;
             BufferContentsWindow::Get(options);
           })});
    }());
}  // namespace
}  // namespace afc::editor
