#include "src/line_output.h"

#include "src/tests/tests.h"

namespace afc::editor {
ColumnNumberDelta LineOutputLength(const Line& line, ColumnNumber begin,
                                   ColumnNumberDelta screen_positions) {
  ColumnNumberDelta output;
  ColumnNumberDelta shown;
  while (begin + output < line.EndColumn() && shown < screen_positions) {
    shown += ColumnNumberDelta(std::max(1, wcwidth(line.get(begin + output))));
    if (shown <= screen_positions || output.IsZero()) ++output;
  }
  return output;
}

namespace {
const bool compute_column_delta_for_output_tests_registration = tests::Register(
    L"LineOutputLength",
    {{.name = L"EmptyAndZero",
      .callback =
          [] {
            CHECK(LineOutputLength({}, {}, ColumnNumberDelta()).IsZero());
          }},
     {.name = L"EmptyAndWants",
      .callback =
          [] {
            CHECK(LineOutputLength({}, {}, ColumnNumberDelta(80)).IsZero());
          }},
     {.name = L"NormalConsumed",
      .callback =
          [] {
            CHECK(LineOutputLength(Line(L"alejandro"), {},
                                   ColumnNumberDelta(80)) ==
                  ColumnNumberDelta(9));
          }},
     {.name = L"NormalOverflow",
      .callback =
          [] {
            CHECK(LineOutputLength(Line(L"alejandro"), {},
                                   ColumnNumberDelta(6)) ==
                  ColumnNumberDelta(6));
          }},
     {.name = L"SimpleWide",
      .callback =
          [] {
            CHECK(LineOutputLength(Line(L"a🦋lejandro"), {},
                                   ColumnNumberDelta(6)) ==
                  ColumnNumberDelta(5));
          }},
     {.name = L"WideConsumed",
      .callback =
          [] {
            CHECK(LineOutputLength(Line(L"a🦋o"), {}, ColumnNumberDelta(6)) ==
                  ColumnNumberDelta(3));
          }},
     {.name = L"CharacterDoesNotFit",
      .callback =
          [] {
            CHECK(LineOutputLength(Line(L"alejo🦋"), {},
                                   ColumnNumberDelta(6)) ==
                  ColumnNumberDelta(5));
          }},
     {.name = L"CharacterAtBorder",
      .callback =
          [] {
            CHECK(LineOutputLength(Line(L"alejo🦋"), {},
                                   ColumnNumberDelta(7)) ==
                  ColumnNumberDelta(6));
          }},
     {.name = L"SingleWidthNormalCharacter",
      .callback =
          [] {
            CHECK(LineOutputLength(Line(L"alejo🦋"), {},
                                   ColumnNumberDelta(1)) ==
                  ColumnNumberDelta(1));
          }},
     {.name = L"SingleWidthWide",
      .callback =
          [] {
            CHECK(LineOutputLength(Line(L"🦋"), {}, ColumnNumberDelta(1)) ==
                  ColumnNumberDelta(1));
          }},
     {.name = L"ManyWideOverflow",
      .callback =
          [] {
            CHECK(LineOutputLength(Line(L"🦋🦋🦋🦋abcdef"), {},
                                   ColumnNumberDelta(5)) ==
                  ColumnNumberDelta(2));
          }},
     {.name = L"ManyWideOverflowAfter",
      .callback =
          [] {
            CHECK(LineOutputLength(Line(L"🦋🦋🦋🦋abcdef"), {},
                                   ColumnNumberDelta(10)) ==
                  ColumnNumberDelta(6));
          }},
     {.name = L"ManyWideOverflowExact", .callback = [] {
        CHECK(LineOutputLength(Line(L"🦋🦋🦋🦋abcdef"), {},
                               ColumnNumberDelta(4)) == ColumnNumberDelta(2));
      }}});
}  // namespace

std::vector<ColumnNumber> BreakLineForOutput(
    const Line& line, ColumnNumberDelta screen_positions) {
  std::vector<ColumnNumber> output = {ColumnNumber{}};
  while (output.back() <= line.EndColumn()) {
    auto start = output.back();
    output.push_back(start + LineOutputLength(line, start, screen_positions));
  }
  if (output.size() > 1) {
    CHECK_EQ(output.back(), line.EndColumn() + ColumnNumberDelta(1));
    output.pop_back();
  }
  return output;
}
}  // namespace afc::editor
