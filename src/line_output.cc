#include "src/line_output.h"

#include "src/tests/tests.h"

namespace afc::editor {
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
      VLOG(8) << "Content based: rewinding, start at: " << output;
      if (begin + output >= line.EndColumn()) {
        VLOG(5) << "Consumed the entire line, no need to wrap.";
        break;
      }
      const ColumnNumberDelta original_output = output;
      while (output > ColumnNumberDelta(1) &&
             symbol_characters.find(line.get(begin + output)) !=
                 symbol_characters.npos)
        --output;  // Scroll back: we're in a symbol.
      if (output <= ColumnNumberDelta(1)) {
        LOG(INFO) << "Giving up, line too short.";
        output = original_output;
      } else if (output != original_output) {
        ++output;
        VLOG(8) << "Content based: advance to beginning of the next symbol: "
                << output;
      }
  }
  VLOG(10) << "Wrap at: " << output;
  return output;
}

namespace {
std::wstring symbol_characters_for_testing = L"abcdefghijklmnopqrstuvwxyz";

const bool compute_column_delta_for_output_tests_registration = tests::Register(
    L"LineOutputLength",
    {{.name = L"EmptyAndZero",
      .callback =
          [] {
            CHECK(LineOutputLength({}, {}, ColumnNumberDelta(),
                                   LineWrapStyle::kBreakWords, L"")
                      .IsZero());
          }},
     {.name = L"EmptyAndWants",
      .callback =
          [] {
            CHECK(LineOutputLength({}, {}, ColumnNumberDelta(80),
                                   LineWrapStyle::kBreakWords, L"")
                      .IsZero());
          }},
     {.name = L"NormalConsumed",
      .callback =
          [] {
            CHECK(LineOutputLength(
                      Line(L"alejandro"), {}, ColumnNumberDelta(80),
                      LineWrapStyle::kBreakWords, L"") == ColumnNumberDelta(9));
          }},
     {.name = L"NormalOverflow",
      .callback =
          [] {
            CHECK(LineOutputLength(Line(L"alejandro"), {}, ColumnNumberDelta(6),
                                   LineWrapStyle::kBreakWords,
                                   L"") == ColumnNumberDelta(6));
          }},
     {.name = L"SimpleWide",
      .callback =
          [] {
            CHECK(LineOutputLength(
                      Line(L"a🦋lejandro"), {}, ColumnNumberDelta(6),
                      LineWrapStyle::kBreakWords, L"") == ColumnNumberDelta(5));
          }},
     {.name = L"WideConsumed",
      .callback =
          [] {
            CHECK(LineOutputLength(Line(L"a🦋o"), {}, ColumnNumberDelta(6),
                                   LineWrapStyle::kBreakWords,
                                   L"") == ColumnNumberDelta(3));
          }},
     {.name = L"CharacterDoesNotFit",
      .callback =
          [] {
            CHECK(LineOutputLength(Line(L"alejo🦋"), {}, ColumnNumberDelta(6),
                                   LineWrapStyle::kBreakWords,
                                   L"") == ColumnNumberDelta(5));
          }},
     {.name = L"CharacterAtBorder",
      .callback =
          [] {
            CHECK(LineOutputLength(Line(L"alejo🦋"), {}, ColumnNumberDelta(7),
                                   LineWrapStyle::kBreakWords,
                                   L"") == ColumnNumberDelta(6));
          }},
     {.name = L"SingleWidthNormalCharacter",
      .callback =
          [] {
            CHECK(LineOutputLength(Line(L"alejo🦋"), {}, ColumnNumberDelta(1),
                                   LineWrapStyle::kBreakWords,
                                   L"") == ColumnNumberDelta(1));
          }},
     {.name = L"SingleWidthWide",
      .callback =
          [] {
            CHECK(LineOutputLength(Line(L"🦋"), {}, ColumnNumberDelta(1),
                                   LineWrapStyle::kBreakWords,
                                   L"") == ColumnNumberDelta(1));
          }},
     {.name = L"ManyWideOverflow",
      .callback =
          [] {
            CHECK(LineOutputLength(
                      Line(L"🦋🦋🦋🦋abcdef"), {}, ColumnNumberDelta(5),
                      LineWrapStyle::kBreakWords, L"") == ColumnNumberDelta(2));
          }},
     {.name = L"ManyWideOverflowAfter",
      .callback =
          [] {
            CHECK(LineOutputLength(Line(L"🦋🦋🦋🦋abcdef"), {},
                                   ColumnNumberDelta(10),
                                   LineWrapStyle::kBreakWords,
                                   L"") == ColumnNumberDelta(6));
          }},
     {.name = L"ManyWideOverflowExact",
      .callback =
          [] {
            CHECK(LineOutputLength(
                      Line(L"🦋🦋🦋🦋abcdef"), {}, ColumnNumberDelta(4),
                      LineWrapStyle::kBreakWords, L"") == ColumnNumberDelta(2));
          }},
     {.name = L"ContentBasedWrapFits",
      .callback =
          [] {
            CHECK(LineOutputLength(Line(L"abcde"), {}, ColumnNumberDelta(10),
                                   LineWrapStyle::kContentBased,
                                   symbol_characters_for_testing) ==
                  ColumnNumberDelta(5));
          }},
     {.name = L"ContentBasedWrapLineWithSpaces",
      .callback =
          [] {
            CHECK(LineOutputLength(
                      Line(L"abcde fghijklmnopqrstuv"), {},
                      ColumnNumberDelta(10), LineWrapStyle::kContentBased,
                      symbol_characters_for_testing) == ColumnNumberDelta(6));
          }},
     {.name = L"ContentBasedWrapLineTooLong", .callback = [] {
        CHECK(LineOutputLength(
                  Line(L"abcdefghijklmnopqrstuv"), {}, ColumnNumberDelta(10),
                  LineWrapStyle::kContentBased,
                  symbol_characters_for_testing) == ColumnNumberDelta(10));
      }}});
}  // namespace

std::vector<ColumnNumber> BreakLineForOutput(const Line& line,
                                             ColumnNumberDelta screen_positions,
                                             LineWrapStyle line_wrap_style,
                                             std::wstring symbol_characters) {
  std::vector<ColumnNumber> output = {ColumnNumber{}};
  while (output.back() <= line.EndColumn()) {
    auto start = output.back();
    output.push_back(start + LineOutputLength(line, start, screen_positions,
                                              line_wrap_style,
                                              symbol_characters));
  }
  if (output.size() > 1) {
    CHECK_EQ(output.back(), line.EndColumn() + ColumnNumberDelta(1));
    output.pop_back();
  }
  return output;
}
}  // namespace afc::editor
