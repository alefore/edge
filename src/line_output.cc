#include "src/line_output.h"

#include "src/language/container.h"
#include "src/tests/tests.h"

namespace container = afc::language::container;
using afc::language::lazy_string::ColumnNumber;
using afc::language::lazy_string::ColumnNumberDelta;
using afc::language::lazy_string::LazyString;
using afc::language::lazy_string::SingleLine;
using afc::language::text::Line;
using afc::language::text::LineBuilder;

namespace afc::editor {
namespace {
ColumnNumberDelta LineOutputLength(
    const Line& line, ColumnNumber begin, ColumnNumberDelta screen_positions,
    LineWrapStyle line_wrap_style,
    std::unordered_set<wchar_t> symbol_characters) {
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
             symbol_characters.contains(line.get(begin + output)))
        --output;  // Scroll back: we're in a symbol.
      if (output <= ColumnNumberDelta(1)) {
        output = original_output;
      } else if (output != original_output) {
        ++output;
      }
  }
  return output;
}

const std::unordered_set<wchar_t> symbol_characters_for_testing =
    container::MaterializeUnorderedSet(
        std::wstring{L"abcdefghijklmnopqrstuvwxyz"});

const bool compute_column_delta_for_output_tests_registration = tests::Register(
    L"LineOutputLength",
    {{.name = L"EmptyAndZero",
      .callback =
          [] {
            CHECK(LineOutputLength({}, ColumnNumber(), ColumnNumberDelta(),
                                   LineWrapStyle::kBreakWords, {})
                      .IsZero());
          }},
     {.name = L"EmptyAndWants",
      .callback =
          [] {
            CHECK(LineOutputLength({}, ColumnNumber(), ColumnNumberDelta(80),
                                   LineWrapStyle::kBreakWords, {})
                      .IsZero());
          }},
     {.name = L"NormalConsumed",
      .callback =
          [] {
            CHECK(LineOutputLength(Line{SingleLine{LazyString{L"alejandro"}}},
                                   ColumnNumber(), ColumnNumberDelta(80),
                                   LineWrapStyle::kBreakWords,
                                   {}) == ColumnNumberDelta(9));
          }},
     {.name = L"NormalOverflow",
      .callback =
          [] {
            CHECK(LineOutputLength(Line{SingleLine{LazyString{L"alejandro"}}},
                                   ColumnNumber(), ColumnNumberDelta(6),
                                   LineWrapStyle::kBreakWords,
                                   {}) == ColumnNumberDelta(6));
          }},
     {.name = L"SimpleWide",
      .callback =
          [] {
            CHECK(LineOutputLength(Line{SingleLine{LazyString{L"a🦋lejandro"}}},
                                   ColumnNumber(), ColumnNumberDelta(6),
                                   LineWrapStyle::kBreakWords,
                                   {}) == ColumnNumberDelta(5));
          }},
     {.name = L"WideConsumed",
      .callback =
          [] {
            CHECK(LineOutputLength(Line{SingleLine{LazyString{L"a🦋o"}}},
                                   ColumnNumber(), ColumnNumberDelta(6),
                                   LineWrapStyle::kBreakWords,
                                   {}) == ColumnNumberDelta(3));
          }},
     {.name = L"CharacterDoesNotFit",
      .callback =
          [] {
            CHECK(LineOutputLength(Line{SingleLine{LazyString{L"alejo🦋"}}},
                                   ColumnNumber(), ColumnNumberDelta(6),
                                   LineWrapStyle::kBreakWords,
                                   {}) == ColumnNumberDelta(5));
          }},
     {.name = L"CharacterAtBorder",
      .callback =
          [] {
            CHECK(LineOutputLength(Line{SingleLine{LazyString{L"alejo🦋"}}},
                                   ColumnNumber(), ColumnNumberDelta(7),
                                   LineWrapStyle::kBreakWords,
                                   {}) == ColumnNumberDelta(6));
          }},
     {.name = L"SingleWidthNormalCharacter",
      .callback =
          [] {
            CHECK(LineOutputLength(Line{SingleLine{LazyString{L"alejo🦋"}}},
                                   ColumnNumber(), ColumnNumberDelta(1),
                                   LineWrapStyle::kBreakWords,
                                   {}) == ColumnNumberDelta(1));
          }},
     {.name = L"SingleWidthWide",
      .callback =
          [] {
            CHECK(LineOutputLength(Line{SingleLine{LazyString{L"🦋"}}},
                                   ColumnNumber(), ColumnNumberDelta(1),
                                   LineWrapStyle::kBreakWords,
                                   {}) == ColumnNumberDelta(1));
          }},
     {.name = L"ManyWideOverflow",
      .callback =
          [] {
            CHECK(LineOutputLength(
                      Line{SingleLine{LazyString{L"🦋🦋🦋🦋abcdef"}}},
                      ColumnNumber(), ColumnNumberDelta(5),
                      LineWrapStyle::kBreakWords, {}) == ColumnNumberDelta(2));
          }},
     {.name = L"ManyWideOverflowAfter",
      .callback =
          [] {
            CHECK(LineOutputLength(
                      Line{SingleLine{LazyString{L"🦋🦋🦋🦋abcdef"}}},
                      ColumnNumber(), ColumnNumberDelta(10),
                      LineWrapStyle::kBreakWords, {}) == ColumnNumberDelta(6));
          }},
     {.name = L"ManyWideOverflowExact",
      .callback =
          [] {
            CHECK(LineOutputLength(
                      Line{SingleLine{LazyString{L"🦋🦋🦋🦋abcdef"}}},
                      ColumnNumber(), ColumnNumberDelta(4),
                      LineWrapStyle::kBreakWords, {}) == ColumnNumberDelta(2));
          }},
     {.name = L"ContentBasedWrapFits",
      .callback =
          [] {
            CHECK(LineOutputLength(
                      Line{SingleLine{LazyString{L"abcde"}}}, ColumnNumber(),
                      ColumnNumberDelta(10), LineWrapStyle::kContentBased,
                      symbol_characters_for_testing) == ColumnNumberDelta(5));
          }},
     {.name = L"ContentBasedWrapLineWithSpaces",
      .callback =
          [] {
            CHECK(LineOutputLength(
                      Line{SingleLine{LazyString{L"abcde fghijklmnopqrstuv"}}},
                      ColumnNumber(), ColumnNumberDelta(10),
                      LineWrapStyle::kContentBased,
                      symbol_characters_for_testing) == ColumnNumberDelta(6));
          }},
     {.name = L"ContentBasedWrapLineTooLong", .callback = [] {
        CHECK(LineOutputLength(
                  Line{SingleLine{LazyString{L"abcdefghijklmnopqrstuv"}}},
                  ColumnNumber(), ColumnNumberDelta(10),
                  LineWrapStyle::kContentBased,
                  symbol_characters_for_testing) == ColumnNumberDelta(10));
      }}});
}  // namespace

std::ostream& operator<<(std::ostream& os,
                         const LineWrapStyle& line_wrap_style) {
  switch (line_wrap_style) {
    case LineWrapStyle::kBreakWords:
      os << "LineWrapStyle::kBreakWords";
      break;
    case LineWrapStyle::kContentBased:
      os << "LineWrapStyle::kContentBase";
      break;
  }
  return os;
}

std::list<ColumnRange> BreakLineForOutput(
    const Line& line, ColumnNumberDelta screen_positions,
    LineWrapStyle line_wrap_style,
    std::unordered_set<wchar_t> symbol_characters) {
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
                                     LineWrapStyle::kBreakWords, {}) ==
                  std::list<ColumnRange>({{ColumnNumber(0), ColumnNumber(0)}}));
          }},
     {.name = L"Fits",
      .callback =
          [] {
            CHECK(BreakLineForOutput(Line{SingleLine{LazyString{L"foo"}}},
                                     ColumnNumberDelta(10),
                                     LineWrapStyle::kBreakWords, {}) ==
                  std::list<ColumnRange>({{ColumnNumber(0), ColumnNumber(3)}}));
          }},
     {.name = L"FitsExactly",
      .callback =
          [] {
            CHECK(BreakLineForOutput(Line{SingleLine{LazyString{L"foobar"}}},
                                     ColumnNumberDelta(6),
                                     LineWrapStyle::kBreakWords, {}) ==
                  std::list<ColumnRange>({{ColumnNumber(0), ColumnNumber(6)}}));
          }},
     {.name = L"Breaks",
      .callback =
          [] {
            CHECK(BreakLineForOutput(
                      Line{SingleLine{LazyString{L"foobarheyyou"}}},
                      ColumnNumberDelta(3), LineWrapStyle::kBreakWords,
                      {}) == std::list<ColumnRange>({
                                 {ColumnNumber(0), ColumnNumber(3)},
                                 {ColumnNumber(3), ColumnNumber(6)},
                                 {ColumnNumber(6), ColumnNumber(9)},
                                 {ColumnNumber(9), ColumnNumber(12)},
                             }));
          }},
     {.name = L"BreaksContentBased",
      .callback =
          [] {
            CHECK(BreakLineForOutput(
                      Line{SingleLine{LazyString{L"foo bar hey"}}},
                      ColumnNumberDelta(5), LineWrapStyle::kContentBased,
                      symbol_characters_for_testing) ==
                  std::list<ColumnRange>({
                      {ColumnNumber(0), ColumnNumber(4)},
                      {ColumnNumber(4), ColumnNumber(8)},
                      {ColumnNumber(8), ColumnNumber(11)},
                  }));
          }},
     {.name = L"BreaksMultipleSpaces", .callback = [] {
        CHECK(BreakLineForOutput(
                  Line{SingleLine{LazyString{L"foo     bar hey"}}},
                  ColumnNumberDelta(5), LineWrapStyle::kContentBased,
                  symbol_characters_for_testing) ==
              std::list<ColumnRange>({
                  {ColumnNumber(0), ColumnNumber(5)},
                  {ColumnNumber(8), ColumnNumber(12)},
                  {ColumnNumber(12), ColumnNumber(15)},
              }));
      }}});
}  // namespace
}  // namespace afc::editor
