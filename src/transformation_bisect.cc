#include "src/transformation_bisect.h"

#include "src/buffer.h"
#include "src/buffer_display_data.h"
#include "src/buffer_variables.h"
#include "src/language/lazy_string/char_buffer.h"
#include "src/language/wstring.h"
#include "src/tests/factory.h"

using afc::infrastructure::screen::Color;
using afc::infrastructure::screen::StandardColor;
using afc::infrastructure::screen::Style;
using afc::infrastructure::screen::StyleAttribute;
using afc::infrastructure::screen::VisualOverlayKey;
using afc::infrastructure::screen::VisualOverlayMap;
using afc::infrastructure::screen::VisualOverlayPriority;
using afc::language::NonNull;
using afc::language::VisitPointer;
using afc::language::lazy_string::ColumnNumber;
using afc::language::lazy_string::ColumnNumberDelta;
using afc::language::lazy_string::LazyString;
using afc::language::lazy_string::SingleLine;
using afc::language::text::LineColumn;
using afc::language::text::LineNumber;
using afc::language::text::LineSequence;
using afc::language::text::Range;

namespace afc::editor::transformation {
using ::operator<<;

Bisect::Bisect(Structure structure, std::vector<Direction> directions)
    : structure_(structure), directions_(std::move(directions)) {}

std::wstring Bisect::Serialize() const { return L"Bisect()"; }

namespace {
LineColumn RangeCenter(const Range& range, Structure structure) {
  if (structure == Structure::Char) {
    return LineColumn(range.begin().line,
                      ColumnNumber() + (range.begin().column.ToDelta() +
                                        range.end().column.ToDelta()) /
                                           2);
  } else if (structure == Structure::Line) {
    if (range.begin().line == range.end().line) return range.begin();
    return LineColumn(
        LineNumber() +
        (range.begin().line.ToDelta() + range.end().line.ToDelta()) / 2);
  }
  LOG(FATAL) << "Invalid structure.";
  return LineColumn();
}

TEST_GROUP(TransformationBisect_RangeCenter, &RangeCenter)
    .Add(L"EmptyRangeChar",
         Range{LineColumn{LineNumber{2}, ColumnNumber{21}},
               LineColumn{LineNumber{2}, ColumnNumber{21}}},
         Structure::Char, LineColumn{LineNumber{2}, ColumnNumber{21}})
    .Add(L"EmptyRangeLine",
         Range{LineColumn{LineNumber{2}, ColumnNumber{21}},
               LineColumn{LineNumber{2}, ColumnNumber{21}}},
         Structure::Line, LineColumn{LineNumber{2}, ColumnNumber{21}})
    .Add(L"NormalRangeChar",
         Range{LineColumn{LineNumber{21}, ColumnNumber{2}},
               LineColumn{LineNumber{21}, ColumnNumber{10}}},
         Structure::Char, LineColumn{LineNumber{21}, ColumnNumber{6}});

Range AdjustRange(Structure structure, Direction direction, Range range) {
  LineColumn center = RangeCenter(range, structure);
  switch (direction) {
    case Direction::Forwards:
      range.set_begin(center);
      break;
    case Direction::Backwards:
      range.set_end(center);
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
                AdjustRange(Structure::Char, Direction::Forwards,
                            Range(LineColumn(LineNumber(2), ColumnNumber(21)),
                                  LineColumn(LineNumber(2), ColumnNumber(21)))),
                Range(LineColumn(LineNumber(2), ColumnNumber(21)),
                      LineColumn(LineNumber(2), ColumnNumber(21))));
          }},
     {.name = L"EmptyRangeCharBackwards",
      .callback =
          [] {
            CHECK_EQ(
                AdjustRange(Structure::Char, Direction::Backwards,
                            Range(LineColumn(LineNumber(2), ColumnNumber(21)),
                                  LineColumn(LineNumber(2), ColumnNumber(21)))),
                Range(LineColumn(LineNumber(2), ColumnNumber(21)),
                      LineColumn(LineNumber(2), ColumnNumber(21))));
          }},
     {.name = L"NormalRangeCharForwards",
      .callback =
          [] {
            CHECK_EQ(
                AdjustRange(Structure::Char, Direction::Forwards,
                            Range(LineColumn(LineNumber(2), ColumnNumber(12)),
                                  LineColumn(LineNumber(2), ColumnNumber(20)))),
                Range(LineColumn(LineNumber(2), ColumnNumber(16)),
                      LineColumn(LineNumber(2), ColumnNumber(20))));
          }},
     {.name = L"NormalRangeCharBackwards", .callback = [] {
        CHECK_EQ(
            AdjustRange(Structure::Char, Direction::Backwards,
                        Range(LineColumn(LineNumber(2), ColumnNumber(12)),
                              LineColumn(LineNumber(2), ColumnNumber(20)))),
            Range(LineColumn(LineNumber(2), ColumnNumber(12)),
                  LineColumn(LineNumber(2), ColumnNumber(16))));
      }}});

Range GetRange(const LineSequence& contents, Direction initial_direction,
               Structure structure, LineColumn position) {
  if (structure == Structure::Char) {
    switch (initial_direction) {
      case Direction::Forwards:
        return Range(
            position,
            LineColumn(position.line, contents.at(position.line)->EndColumn()));

      case Direction::Backwards:
        return Range(LineColumn(position.line, ColumnNumber()), position);
    }
  } else if (structure == Structure::Line) {
    switch (initial_direction) {
      case Direction::Forwards:
        return Range(position, LineColumn(contents.EndLine(),
                                          contents.back()->EndColumn()));
      case Direction::Backwards:
        return Range(LineColumn(), position);
    }
  }
  LOG(FATAL) << "Invalid structure: " << structure;
  return Range();
}

TEST_GROUP(TransformationBisect_GetRange_Empty, &GetRange)
    .Add(L"EmptyBufferCharForwards", LineSequence{}, Direction::Forwards,
         Structure::Char, LineColumn{}, Range{})
    .Add(L"EmptyBufferCharBackwards", LineSequence{}, Direction::Backwards,
         Structure::Char, LineColumn{}, Range{})
    .Add(L"EmptyBufferLineForwards", LineSequence{}, Direction::Forwards,
         Structure::Line, LineColumn{}, Range{})
    .Add(L"EmptyBufferLineBackwards", LineSequence{}, Direction::Backwards,
         Structure::Line, LineColumn{}, Range{});

TEST_GROUP(TransformationBisect_GetRange_NonEmpty,
           [](Direction d, Structure s, LineColumn p) {
             return GetRange(LineSequence::ForTests(
                                 {L"", L"Alejandro", L"Forero", L"Cuervo"}),
                             d, s, p);
           })
    .Add(L"NonEmptyBufferCharForwards", Direction::Forwards, Structure::Char,
         LineColumn{LineNumber{1}, ColumnNumber{4}},
         Range{LineColumn{LineNumber{1}, ColumnNumber{4}},
               LineColumn{LineNumber{1}, ColumnNumber{9}}})
    .Add(L"NonEmptyBufferCharBackwards", Direction::Backwards, Structure::Char,
         LineColumn{LineNumber{1}, ColumnNumber{4}},
         Range{LineColumn{LineNumber{1}, ColumnNumber{0}},
               LineColumn{LineNumber{1}, ColumnNumber{4}}})
    .Add(L"NonEmptyBufferLineForwards", Direction::Forwards, Structure::Line,
         LineColumn{LineNumber{1}, ColumnNumber{4}},
         Range{LineColumn{LineNumber{1}, ColumnNumber{4}},
               LineColumn{LineNumber{3}, ColumnNumber{6}}})
    .Add(L"NonEmptyBufferLineBackwards", Direction::Backwards, Structure::Line,
         LineColumn{LineNumber{1}, ColumnNumber{4}},
         Range{LineColumn{LineNumber{0}, ColumnNumber{0}},
               LineColumn{LineNumber{1}, ColumnNumber{4}}});
}  // namespace

futures::Value<CompositeTransformation::Output> Bisect::Apply(
    CompositeTransformation::Input input) const {
  LineSequence snapshot = input.buffer.contents().snapshot();

  const std::optional<Range> range = std::ranges::fold_left(
      directions_, std::optional<Range>{},
      [this, &snapshot, &input](std::optional<Range> output,
                                Direction direction) -> std::optional<Range> {
        if (output)
          return AdjustRange(structure_, direction, output.value());
        else
          return GetRange(snapshot, direction, structure_, input.position);
      });

  if (!range) return Output{};

  LineColumn center = RangeCenter(range.value(), structure_);
  CompositeTransformation::Output output =
      CompositeTransformation::Output::SetPosition(center);

  static const VisualOverlayPriority kPriority = VisualOverlayPriority(1);
  static const VisualOverlayKey kKey = VisualOverlayKey(L"bisect");
  switch (input.mode) {
    case transformation::Input::Mode::Final:
      break;
    case transformation::Input::Mode::Preview:
      VisualOverlayMap overlays;
      if (range.value().begin() != center)
        overlays[kPriority][kKey].insert(
            {range.value().begin(),
             afc::infrastructure::screen::VisualOverlay{
                 .content = SingleLine{LazyString{L"⟦"}},
                 .modifiers = Style{.attributes = StyleAttribute::Reverse}}});
      if (range.value().end() != center)
        overlays[kPriority][kKey].insert(
            {range.value().end(),
             afc::infrastructure::screen::VisualOverlay{
                 .content = SingleLine{LazyString{L"⟧"}},
                 .modifiers = Style{.attributes = StyleAttribute::Reverse}}});
      output.Push(VisualOverlay{.visual_overlay_map = std::move(overlays)});
      break;
  }
  return output;
}

}  // namespace afc::editor::transformation
