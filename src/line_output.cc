#include "src/line_output.h"

#include "src/tests/tests.h"

namespace afc::editor {
namespace {
ColumnNumberDelta LineOutputLength(const Line& line, ColumnNumber begin,
                                   ColumnNumberDelta screen_positions,
                                   LineWrapStyle line_wrap_style,
                                   std::wstring symbol_characters) {
  ColumnNumberDelta output;
  ColumnNumberDelta shown;
  while (begin + output < line.EndColumn() && shown < screen_positions) {
    shown += ColumnNumberDelta(std::max(1, wcwidth(line.get(begin + output))));
    if (shown <= screen_positions || output.IsZero()) ++output;
  }
  switch (line_wrap_style) {
    case LineWrapStyle::kBreakWords:
      break;
    case LineWrapStyle::kContentBased:
      if (begin + output >= line.EndColumn()) {
        break;
      }
      const ColumnNumberDelta original_output = output;
      while (output > ColumnNumberDelta(1) &&
             symbol_characters.find(line.get(begin + output)) !=
                 symbol_characters.npos)
        --output;  // Scroll back: we're in a symbol.
      if (output <= ColumnNumberDelta(1)) {
        output = original_output;
      } else if (output != original_output) {
        ++output;
      }
  }
  return output;
}

std::wstring symbol_characters_for_testing = L"abcdefghijklmnopqrstuvwxyz";

const bool compute_column_delta_for_output_tests_registration = tests::Register(
    L"LineOutputLength",
    {{.name = L"EmptyAndZero",
      .callback =
          [] {
            CHECK(LineOutputLength({}, ColumnNumber(), ColumnNumberDelta(),
                                   LineWrapStyle::kBreakWords, L"")
                      .IsZero());
          }},
     {.name = L"EmptyAndWants",
      .callback =
          [] {
            CHECK(LineOutputLength({}, ColumnNumber(), ColumnNumberDelta(80),
                                   LineWrapStyle::kBreakWords, L"")
                      .IsZero());
          }},
     {.name = L"NormalConsumed",
      .callback =
          [] {
            CHECK(LineOutputLength(
                      Line(L"alejandro"), ColumnNumber(), ColumnNumberDelta(80),
                      LineWrapStyle::kBreakWords, L"") == ColumnNumberDelta(9));
          }},
     {.name = L"NormalOverflow",
      .callback =
          [] {
            CHECK(LineOutputLength(
                      Line(L"alejandro"), ColumnNumber(), ColumnNumberDelta(6),
                      LineWrapStyle::kBreakWords, L"") == ColumnNumberDelta(6));
          }},
     {.name = L"SimpleWide",
      .callback =
          [] {
            CHECK(LineOutputLength(Line(L"a🦋lejandro"), ColumnNumber(),
                                   ColumnNumberDelta(6),
                                   LineWrapStyle::kBreakWords,
                                   L"") == ColumnNumberDelta(5));
          }},
     {.name = L"WideConsumed",
      .callback =
          [] {
            CHECK(LineOutputLength(
                      Line(L"a🦋o"), ColumnNumber(), ColumnNumberDelta(6),
                      LineWrapStyle::kBreakWords, L"") == ColumnNumberDelta(3));
          }},
     {.name = L"CharacterDoesNotFit",
      .callback =
          [] {
            CHECK(LineOutputLength(
                      Line(L"alejo🦋"), ColumnNumber(), ColumnNumberDelta(6),
                      LineWrapStyle::kBreakWords, L"") == ColumnNumberDelta(5));
          }},
     {.name = L"CharacterAtBorder",
      .callback =
          [] {
            CHECK(LineOutputLength(
                      Line(L"alejo🦋"), ColumnNumber(), ColumnNumberDelta(7),
                      LineWrapStyle::kBreakWords, L"") == ColumnNumberDelta(6));
          }},
     {.name = L"SingleWidthNormalCharacter",
      .callback =
          [] {
            CHECK(LineOutputLength(
                      Line(L"alejo🦋"), ColumnNumber(), ColumnNumberDelta(1),
                      LineWrapStyle::kBreakWords, L"") == ColumnNumberDelta(1));
          }},
     {.name = L"SingleWidthWide",
      .callback =
          [] {
            CHECK(LineOutputLength(
                      Line(L"🦋"), ColumnNumber(), ColumnNumberDelta(1),
                      LineWrapStyle::kBreakWords, L"") == ColumnNumberDelta(1));
          }},
     {.name = L"ManyWideOverflow",
      .callback =
          [] {
            CHECK(LineOutputLength(Line(L"🦋🦋🦋🦋abcdef"),
                                   ColumnNumber(), ColumnNumberDelta(5),
                                   LineWrapStyle::kBreakWords,
                                   L"") == ColumnNumberDelta(2));
          }},
     {.name = L"ManyWideOverflowAfter",
      .callback =
          [] {
            CHECK(LineOutputLength(Line(L"🦋🦋🦋🦋abcdef"),
                                   ColumnNumber(), ColumnNumberDelta(10),
                                   LineWrapStyle::kBreakWords,
                                   L"") == ColumnNumberDelta(6));
          }},
     {.name = L"ManyWideOverflowExact",
      .callback =
          [] {
            CHECK(LineOutputLength(Line(L"🦋🦋🦋🦋abcdef"),
                                   ColumnNumber(), ColumnNumberDelta(4),
                                   LineWrapStyle::kBreakWords,
                                   L"") == ColumnNumberDelta(2));
          }},
     {.name = L"ContentBasedWrapFits",
      .callback =
          [] {
            CHECK(LineOutputLength(
                      Line(L"abcde"), ColumnNumber(), ColumnNumberDelta(10),
                      LineWrapStyle::kContentBased,
                      symbol_characters_for_testing) == ColumnNumberDelta(5));
          }},
     {.name = L"ContentBasedWrapLineWithSpaces",
      .callback =
          [] {
            CHECK(LineOutputLength(
                      Line(L"abcde fghijklmnopqrstuv"), ColumnNumber(),
                      ColumnNumberDelta(10), LineWrapStyle::kContentBased,
                      symbol_characters_for_testing) == ColumnNumberDelta(6));
          }},
     {.name = L"ContentBasedWrapLineTooLong", .callback = [] {
        CHECK(LineOutputLength(
                  Line(L"abcdefghijklmnopqrstuv"), ColumnNumber(),
                  ColumnNumberDelta(10), LineWrapStyle::kContentBased,
                  symbol_characters_for_testing) == ColumnNumberDelta(10));
      }}});
}  // namespace

std::list<ColumnRange> BreakLineForOutput(const Line& line,
                                          ColumnNumberDelta screen_positions,
                                          LineWrapStyle line_wrap_style,
                                          std::wstring symbol_characters) {
  std::list<ColumnRange> output;
  ColumnNumber start;
  while (output.empty() || start < line.EndColumn()) {
    output.push_back(
        {.begin = start,
         .end = start + LineOutputLength(line, start, screen_positions,
                                         line_wrap_style, symbol_characters)});
    start = output.back().end;
    switch (line_wrap_style) {
      case LineWrapStyle::kBreakWords:
        break;
      case LineWrapStyle::kContentBased:
        while (start < line.EndColumn() && line.get(start) == L' ') {
          ++start;
        }
    }
  }
  return output;
}

namespace {
const bool break_line_for_output_tests_registration = tests::Register(
    L"BreakLineForOutput",
    {{.name = L"Empty",
      .callback =
          [] {
            CHECK(BreakLineForOutput({}, ColumnNumberDelta(10),
                                     LineWrapStyle::kBreakWords, L"") ==
                  std::list<ColumnRange>({{ColumnNumber(0), ColumnNumber(0)}}));
          }},
     {.name = L"Fits",
      .callback =
          [] {
            CHECK(BreakLineForOutput(Line(L"foo"), ColumnNumberDelta(10),
                                     LineWrapStyle::kBreakWords, L"") ==
                  std::list<ColumnRange>({{ColumnNumber(0), ColumnNumber(3)}}));
          }},
     {.name = L"FitsExactly",
      .callback =
          [] {
            CHECK(BreakLineForOutput(Line(L"foobar"), ColumnNumberDelta(6),
                                     LineWrapStyle::kBreakWords, L"") ==
                  std::list<ColumnRange>({{ColumnNumber(0), ColumnNumber(6)}}));
          }},
     {.name = L"Breaks",
      .callback =
          [] {
            CHECK(BreakLineForOutput(Line(L"foobarheyyou"),
                                     ColumnNumberDelta(3),
                                     LineWrapStyle::kBreakWords, L"") ==
                  std::list<ColumnRange>({
                      {ColumnNumber(0), ColumnNumber(3)},
                      {ColumnNumber(3), ColumnNumber(6)},
                      {ColumnNumber(6), ColumnNumber(9)},
                      {ColumnNumber(9), ColumnNumber(12)},
                  }));
          }},
     {.name = L"BreaksContentBased",
      .callback =
          [] {
            CHECK(BreakLineForOutput(Line(L"foo bar hey"), ColumnNumberDelta(5),
                                     LineWrapStyle::kContentBased,
                                     symbol_characters_for_testing) ==
                  std::list<ColumnRange>({
                      {ColumnNumber(0), ColumnNumber(4)},
                      {ColumnNumber(4), ColumnNumber(8)},
                      {ColumnNumber(8), ColumnNumber(11)},
                  }));
          }},
     {.name = L"BreaksMultipleSpaces", .callback = [] {
        CHECK(BreakLineForOutput(Line(L"foo     bar hey"), ColumnNumberDelta(5),
                                 LineWrapStyle::kContentBased,
                                 symbol_characters_for_testing) ==
              std::list<ColumnRange>({
                  {ColumnNumber(0), ColumnNumber(5)},
                  {ColumnNumber(8), ColumnNumber(12)},
                  {ColumnNumber(12), ColumnNumber(15)},
              }));
      }}});
}
}  // namespace afc::editor
