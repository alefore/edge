#include "src/transformation/bisect.h"

#include "src/buffer.h"
#include "src/buffer_display_data.h"
#include "src/buffer_variables.h"
#include "src/language/lazy_string/append.h"
#include "src/language/lazy_string/char_buffer.h"
#include "src/language/wstring.h"
#include "src/tests/tests.h"

namespace afc::editor::transformation {
using afc::language::VisitPointer;
using afc::language::lazy_string::Append;
using afc::language::lazy_string::ColumnNumber;
using afc::language::lazy_string::ColumnNumberDelta;
using afc::language::lazy_string::NewLazyString;
using ::operator<<;

Bisect::Bisect(Structure* structure, std::vector<Direction> directions)
    : structure_(structure), directions_(std::move(directions)) {}

std::wstring Bisect::Serialize() const { return L"Bisect()"; }

namespace {
LineColumn RangeCenter(const Range& range, Structure* structure) {
  if (structure == StructureChar()) {
    return LineColumn(
        range.begin.line,
        ColumnNumber() +
            (range.begin.column.ToDelta() + range.end.column.ToDelta()) / 2);
  } else if (structure == StructureLine()) {
    if (range.begin.line == range.end.line) return range.begin;
    return LineColumn(LineNumber() +
                      (range.begin.line.ToDelta() + range.end.line.ToDelta()) /
                          2);
  }
  LOG(FATAL) << "Invalid structure.";
  return LineColumn();
}

const bool range_center_tests_registration = tests::Register(
    L"Bisect::RangeCenter",
    {{.name = L"EmptyRangeChar",
      .callback =
          [] {
            CHECK_EQ(
                RangeCenter(Range(LineColumn(LineNumber(2), ColumnNumber(21)),
                                  LineColumn(LineNumber(2), ColumnNumber(21))),
                            StructureChar()),
                LineColumn(LineNumber(2), ColumnNumber(21)));
          }},
     {.name = L"EmptyRangeLine",
      .callback =
          [] {
            CHECK_EQ(
                RangeCenter(Range(LineColumn(LineNumber(2), ColumnNumber(21)),
                                  LineColumn(LineNumber(2), ColumnNumber(21))),
                            StructureLine()),
                LineColumn(LineNumber(2), ColumnNumber(21)));
          }},
     {.name = L"NormalRangeChar", .callback = [] {
        CHECK_EQ(
            RangeCenter(Range(LineColumn(LineNumber(21), ColumnNumber(2)),
                              LineColumn(LineNumber(21), ColumnNumber(10))),
                        StructureChar()),
            LineColumn(LineNumber(21), ColumnNumber(6)));
      }}});

Range AdjustRange(Structure* structure, Direction direction, Range range) {
  LineColumn center = RangeCenter(range, structure);
  switch (direction) {
    case Direction::kForwards:
      range.begin = center;
      break;
    case Direction::kBackwards:
      range.end = center;
      break;
  }
  return range;
}

const bool adjust_range_tests_registration = tests::Register(
    L"Bisect::AdjustRange",
    {{.name = L"EmptyRangeCharForwards",
      .callback =
          [] {
            CHECK_EQ(
                AdjustRange(StructureChar(), Direction::kForwards,
                            Range(LineColumn(LineNumber(2), ColumnNumber(21)),
                                  LineColumn(LineNumber(2), ColumnNumber(21)))),
                Range(LineColumn(LineNumber(2), ColumnNumber(21)),
                      LineColumn(LineNumber(2), ColumnNumber(21))));
          }},
     {.name = L"EmptyRangeCharBackwards",
      .callback =
          [] {
            CHECK_EQ(
                AdjustRange(StructureChar(), Direction::kBackwards,
                            Range(LineColumn(LineNumber(2), ColumnNumber(21)),
                                  LineColumn(LineNumber(2), ColumnNumber(21)))),
                Range(LineColumn(LineNumber(2), ColumnNumber(21)),
                      LineColumn(LineNumber(2), ColumnNumber(21))));
          }},
     {.name = L"NormalRangeCharForwards",
      .callback =
          [] {
            CHECK_EQ(
                AdjustRange(StructureChar(), Direction::kForwards,
                            Range(LineColumn(LineNumber(2), ColumnNumber(12)),
                                  LineColumn(LineNumber(2), ColumnNumber(20)))),
                Range(LineColumn(LineNumber(2), ColumnNumber(16)),
                      LineColumn(LineNumber(2), ColumnNumber(20))));
          }},
     {.name = L"NormalRangeCharBackwards", .callback = [] {
        CHECK_EQ(
            AdjustRange(StructureChar(), Direction::kBackwards,
                        Range(LineColumn(LineNumber(2), ColumnNumber(12)),
                              LineColumn(LineNumber(2), ColumnNumber(20)))),
            Range(LineColumn(LineNumber(2), ColumnNumber(12)),
                  LineColumn(LineNumber(2), ColumnNumber(16))));
      }}});

Range GetRange(const OpenBuffer& buffer, Direction initial_direction,
               Structure* structure, LineColumn position) {
  if (structure == StructureChar()) {
    switch (initial_direction) {
      case Direction::kForwards:
        return Range(
            position,
            LineColumn(
                position.line,
                buffer.contents().at(position.line).value().EndColumn()));

      case Direction::kBackwards:
        return Range(LineColumn(position.line, ColumnNumber()), position);
    }
  } else if (structure == StructureLine()) {
    switch (initial_direction) {
      case Direction::kForwards:
        return Range(position, buffer.end_position());
      case Direction::kBackwards:
        return Range(LineColumn(), position);
    }
  }
  LOG(FATAL) << "Invalid structure: " << structure;
  return Range();
}

const bool get_range_tests_registration = [] {
  using afc::language::gc::Root;
  auto non_empty_buffer = [] {
    Root<OpenBuffer> output = NewBufferForTests();
    output.ptr().value().AppendLazyString(
        NewLazyString(L"Alejandro\nForero\nCuervo"));
    return output;
  };
  return tests::Register(
      L"Bisect::GetRange",
      {{.name = L"EmptyBufferCharForwards",
        .callback =
            [] {
              CHECK_EQ(
                  GetRange(NewBufferForTests().ptr().value(),
                           Direction::kForwards, StructureChar(), LineColumn()),
                  Range());
            }},
       {.name = L"EmptyBufferCharBackwards",
        .callback =
            [] {
              CHECK_EQ(GetRange(NewBufferForTests().ptr().value(),
                                Direction::kBackwards, StructureChar(),
                                LineColumn()),
                       Range());
            }},
       {.name = L"EmptyBufferLineForwards",
        .callback =
            [] {
              CHECK_EQ(
                  GetRange(NewBufferForTests().ptr().value(),
                           Direction::kForwards, StructureLine(), LineColumn()),
                  Range());
            }},
       {.name = L"EmptyBufferLineBackwards",
        .callback =
            [] {
              CHECK_EQ(GetRange(NewBufferForTests().ptr().value(),
                                Direction::kBackwards, StructureLine(),
                                LineColumn()),
                       Range());
            }},
       {.name = L"NonEmptyBufferCharForwards",
        .callback =
            [&] {
              CHECK_EQ(GetRange(non_empty_buffer().ptr().value(),
                                Direction::kForwards, StructureChar(),
                                LineColumn(LineNumber(1), ColumnNumber(4))),
                       Range(LineColumn(LineNumber(1), ColumnNumber(4)),
                             LineColumn(LineNumber(1), ColumnNumber(9))));
            }},
       {.name = L"NonEmptyBufferCharBackwards",
        .callback =
            [&] {
              CHECK_EQ(GetRange(non_empty_buffer().ptr().value(),
                                Direction::kBackwards, StructureChar(),
                                LineColumn(LineNumber(1), ColumnNumber(4))),
                       Range(LineColumn(LineNumber(1), ColumnNumber(0)),
                             LineColumn(LineNumber(1), ColumnNumber(4))));
            }},
       {.name = L"NonEmptyBufferLineForwards",
        .callback =
            [&] {
              CHECK_EQ(GetRange(non_empty_buffer().ptr().value(),
                                Direction::kForwards, StructureLine(),
                                LineColumn(LineNumber(1), ColumnNumber(4))),
                       Range(LineColumn(LineNumber(1), ColumnNumber(4)),
                             LineColumn(LineNumber(3), ColumnNumber(6))));
            }},
       {.name = L"NonEmptyBufferLineBackwards", .callback = [&] {
          CHECK_EQ(GetRange(non_empty_buffer().ptr().value(),
                            Direction::kBackwards, StructureLine(),
                            LineColumn(LineNumber(1), ColumnNumber(4))),
                   Range(LineColumn(LineNumber(0), ColumnNumber(0)),
                         LineColumn(LineNumber(1), ColumnNumber(4))));
        }}});
}();

}  // namespace

futures::Value<CompositeTransformation::Output> Bisect::Apply(
    CompositeTransformation::Input input) const {
  std::optional<Range> range;
  for (Direction direction : directions_)
    if (range == std::nullopt)
      range = GetRange(input.buffer, direction, structure_, input.position);
    else
      range = AdjustRange(structure_, direction, range.value());

  if (range == std::nullopt) return futures::Past(Output());
  LineColumn center = RangeCenter(range.value(), structure_);
  CompositeTransformation::Output output =
      CompositeTransformation::Output::SetPosition(center);

  switch (input.mode) {
    case transformation::Input::Mode::kFinal:
      break;
    case transformation::Input::Mode::kPreview:
      VisualOverlayMap overlays;
      if (range.value().begin != center)
        overlays.insert(
            {range.value().begin,
             afc::editor::VisualOverlay{
                 .content = std::move(NewLazyString(L"⟦").get_unique()),
                 .modifiers = {LineModifier::REVERSE}}});
      if (range.value().end != center)
        overlays.insert(
            {range.value().end,
             afc::editor::VisualOverlay{
                 .content = std::move(NewLazyString(L"⟧").get_unique()),
                 .modifiers = {LineModifier::REVERSE}}});
      output.Push(VisualOverlay(std::move(overlays)));
      break;
  }
  return futures::Past(std::move(output));
}

}  // namespace afc::editor::transformation