#include "src/transformation_bisect.h"

#include <ranges>

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
using afc::language::text::LineNumberDelta;
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

TEST_GROUP(TransformationBisect_AdjustRange, &AdjustRange)
    .Add(L"EmptyRangeCharForwards", Structure::Char, Direction::Forwards,
         Range{LineColumn{LineNumber{2}, ColumnNumber{21}},
               LineColumn{LineNumber{2}, ColumnNumber{21}}},
         Range{LineColumn{LineNumber{2}, ColumnNumber{21}},
               LineColumn{LineNumber{2}, ColumnNumber{21}}})
    .Add(L"EmptyRangeCharBackwards", Structure::Char, Direction::Backwards,
         Range{LineColumn{LineNumber{2}, ColumnNumber{21}},
               LineColumn{LineNumber{2}, ColumnNumber{21}}},
         Range{LineColumn{LineNumber{2}, ColumnNumber{21}},
               LineColumn{LineNumber{2}, ColumnNumber{21}}})
    .Add(L"NormalRangeCharForwards", Structure::Char, Direction::Forwards,
         Range{LineColumn{LineNumber{2}, ColumnNumber{12}},
               LineColumn{LineNumber{2}, ColumnNumber{20}}},
         Range{LineColumn{LineNumber{2}, ColumnNumber{16}},
               LineColumn{LineNumber{2}, ColumnNumber{20}}})
    .Add(L"NormalRangeCharBackwards", Structure::Char, Direction::Backwards,
         Range{LineColumn{LineNumber{2}, ColumnNumber{12}},
               LineColumn{LineNumber{2}, ColumnNumber{20}}},
         Range{LineColumn{LineNumber{2}, ColumnNumber{12}},
               LineColumn{LineNumber{2}, ColumnNumber{16}}});

template <typename Position, typename PositionDelta>
Position GetBoundaryOneDimension(Position start, PositionDelta size,
                                 Direction direction, size_t jumps) {
  if (jumps > 60)  // Just give up.
    return direction == Direction::Forwards ? Position{} + size : Position{};
  PositionDelta total_jump{static_cast<int>((1 << (jumps + 2)) - 4)};
  switch (direction) {
    case Direction::Forwards:
      return size - start.ToDelta() <= total_jump ? Position{} + size
                                                  : start + total_jump;
    case Direction::Backwards:
      return start.ToDelta() >= total_jump ? start - total_jump : Position{};
  }
  LOG(FATAL) << "Invalid direction.";
  std::unreachable();
}

TEST_GROUP(TransformationBisect_GetBoundaryOneDimension_Basic,
           [](size_t jumps) {
             return GetBoundaryOneDimension(LineNumber{0},
                                            LineNumberDelta{1000},
                                            Direction::Forwards, jumps)
                 .read();
           })
    .Add(L"Forward:1", 1, 4)
    .Add(L"Forward:2", 2, 12)
    .Add(L"Forward:3", 3, 28)
    .Add(L"Forward:4", 4, 60)
    .Add(L"Forward:5", 5, 124);

LineColumn GetBoundary(LineSequence contents, Direction direction,
                       size_t prefix_length, Structure structure,
                       LineColumn position) {
  switch (structure) {
    case Structure::Char:
      return LineColumn{position.line,
                        GetBoundaryOneDimension(
                            position.column,
                            contents.at(position.line)->EndColumn().ToDelta(),
                            direction, prefix_length)};
    case Structure::Line:
      return LineColumn{
          GetBoundaryOneDimension(position.line, contents.EndLine().ToDelta(),
                                  direction, prefix_length),
          position.column};
    default:
      LOG(FATAL) << "Invalid structure for Bisect transformation.";
      std::unreachable();
  }
}

TEST_GROUP(TransformationBisect_GetBoundary,
           [](Direction direction, size_t prefix_length, Structure structure,
              LineColumn position) {
             return GetBoundary(LineSequence::ForTests(
                                    {L"", L"Alejandro likes to go to the park.",
                                     L"Forero", L"Cuervo", L"", L"", L"", L"",
                                     L"", L"", L"", L"", L"", L""}),
                                direction, prefix_length, structure, position);
           })
    .Add(L"SingleForwardChar", Direction::Forwards, 1, Structure::Char,
         LineColumn{LineNumber{1}}, LineColumn{LineNumber{1}, ColumnNumber{4}})
    .Add(L"SingleBackwardChar", Direction::Backwards, 1, Structure::Char,
         LineColumn{LineNumber{1}, ColumnNumber{5}},
         LineColumn{LineNumber{1}, ColumnNumber{1}})
    .Add(L"TwoForwardChar", Direction::Forwards, 2, Structure::Char,
         LineColumn{LineNumber{1}}, LineColumn{LineNumber{1}, ColumnNumber{12}})
    .Add(L"MultiForwardChar", Direction::Forwards, 3, Structure::Char,
         LineColumn{LineNumber{1}, ColumnNumber{1}},
         LineColumn{LineNumber{1}, ColumnNumber{29}})
    .Add(L"MultiBackwardChar", Direction::Backwards, 4, Structure::Char,
         LineColumn{LineNumber{1}, ColumnNumber{64}},
         LineColumn{LineNumber{1}, ColumnNumber{64 - 60}})
    .Add(L"CappedForwardChar", Direction::Forwards, 20, Structure::Char,
         LineColumn{LineNumber{1}, ColumnNumber{7}},
         LineColumn{LineNumber{1}, ColumnNumber{34}})
    .Add(L"CappedBackwardChar", Direction::Backwards, 7, Structure::Char,
         LineColumn{LineNumber{1}, ColumnNumber{5}}, LineColumn{LineNumber{1}})
    .Add(L"SingleForwardLine", Direction::Forwards, 1, Structure::Line,
         LineColumn{LineNumber{1}, ColumnNumber{20}},
         LineColumn{LineNumber{5}, ColumnNumber{20}})
    .Add(L"SingleBackwardLine", Direction::Backwards, 1, Structure::Line,
         LineColumn{LineNumber{5}, ColumnNumber{4}},
         LineColumn{LineNumber{1}, ColumnNumber{4}})
    .Add(L"MultiForwardLine", Direction::Forwards, 2, Structure::Line,
         LineColumn{LineNumber{0}}, LineColumn{LineNumber{12}})
    .Add(L"MultiBackwardLine", Direction::Backwards, 2, Structure::Line,
         LineColumn{LineNumber{17}, ColumnNumber{30}},
         LineColumn{LineNumber{5}, ColumnNumber{30}})
    .Add(L"CappedForwardLine", Direction::Forwards, 20, Structure::Line,
         LineColumn{LineNumber{1}, ColumnNumber{7}},
         LineColumn{LineNumber{13}, ColumnNumber{7}})
    .Add(L"CappedBackwardLine", Direction::Backwards, 7, Structure::Line,
         LineColumn{LineNumber{1}}, LineColumn{LineNumber{0}});

struct DirectionsData {
  Direction initial = Direction::Forwards;
  size_t prefix_length = 0;
  std::vector<Direction> tail = {};

  std::variant<Range, LineColumn> ComputeRange(const LineSequence& contents,
                                               LineColumn position,
                                               Structure structure) const {
    // We assume that the neighboring position is NOT the target (otherwise the
    // user would have just moved into it, rather than using bisect).
    if (prefix_length) {
      auto AdjustPosition = [&](auto& value, const auto& limit) {
        switch (initial) {
          case Direction::Forwards:
            if (value < limit) ++value;
            break;
          case Direction::Backwards:
            if (!value.IsZero()) --value;
            break;
        }
      };
      switch (structure) {
        case Structure::Char:
          AdjustPosition(position.column,
                         contents.at(position.line)->EndColumn());
          break;
        case Structure::Line:
          AdjustPosition(position.line, contents.EndLine());
          break;
        default:
          LOG(FATAL) << "Invalid structure.";
      }
    }

    LineColumn range_boundary =
        GetBoundary(contents, initial, prefix_length, structure, position);

    if (tail.empty()) return range_boundary;

    LineColumn range_boundary_prev =
        GetBoundary(contents, initial, prefix_length - 1, structure, position);

    // Why do we skip the first element of directions.tail? That one is simply
    // used to set the initial range; if we didn't skip it here, we would be
    // double-processing it.
    return std::ranges::fold_left(
        tail | std::views::drop(1),
        Range{std::min(range_boundary, range_boundary_prev),
              std::max(range_boundary, range_boundary_prev)},
        [structure](Range output, Direction direction) -> Range {
          return AdjustRange(structure, direction, output);
        });
  }

  bool operator==(const DirectionsData&) const = default;
};

std::ostream& operator<<(std::ostream& os, const DirectionsData& data) {
  os << "[directions data: initial:"
     << (data.initial == Direction::Forwards ? "Forwards" : "Backwards")
     << ", prefix_length: " << data.prefix_length
     << ",[tail size:" << data.tail.size();
  return os;
}

TEST_GROUP(TransformationBisect_DirectionsData_ComputeRange,
           [](DirectionsData data) {
             return data.ComputeRange(
                 LineSequence::ForTests(
                     {L"Alejandro Cuervo likes to go to the lake."}),
                 LineColumn{LineNumber{0}, ColumnNumber{2}}, Structure::Char);
           })
    .Add(L"NoMove", DirectionsData{},
         LineColumn{LineNumber{0}, ColumnNumber{2}})
    .Add(L"Move:1", DirectionsData{.prefix_length = 1},
         LineColumn{LineNumber{0}, ColumnNumber{7}})
    .Add(L"Move:2", DirectionsData{.prefix_length = 2},
         LineColumn{LineNumber{0}, ColumnNumber{15}})
    .Add(L"Move:3", DirectionsData{.prefix_length = 3},
         LineColumn{LineNumber{0}, ColumnNumber{31}})
    .Add(L"SingleTail",
         DirectionsData{.prefix_length = 3, .tail = {Direction::Backwards}},
         Range{LineColumn{LineNumber{0}, ColumnNumber{15}},
               LineColumn{LineNumber{0}, ColumnNumber{31}}})
    .Add(L"FancyTail",
         DirectionsData{.prefix_length = 3,
                        .tail = {Direction::Backwards, Direction::Forwards,
                                 Direction::Backwards}},
         Range{LineColumn{LineNumber{0}, ColumnNumber{23}},
               LineColumn{LineNumber{0}, ColumnNumber{27}}});

DirectionsData ProcessDirections(const std::vector<Direction>& input) {
  if (input.empty()) return DirectionsData{};
  auto split_it = std::ranges::adjacent_find(input, std::not_equal_to{});
  if (split_it != input.end())
    // adjacent_find returns the iterator to the *first* element of a
    // non-equal pair, so we advance to the first different element.
    std::ranges::advance(split_it, 1);

  return DirectionsData{
      .initial = input[0],
      .prefix_length = split_it == input.end()
                           ? input.size()
                           : std::ranges::distance(input.begin(), split_it),
      .tail = std::ranges::subrange(split_it, input.end()) |
              std::ranges::to<std::vector>()};
}

TEST_GROUP(TransformationBisect_ProcessDirections, &ProcessDirections)
    .Add(L"Empty", {}, DirectionsData{})
    .Add(L"SingleForwards", {Direction::Forwards},
         DirectionsData{
             .initial = Direction::Forwards, .prefix_length = 1, .tail = {}})
    .Add(L"TwoForwards", {Direction::Forwards, Direction::Forwards},
         DirectionsData{
             .initial = Direction::Forwards, .prefix_length = 2, .tail = {}})
    .Add(L"SingleBackwards", {Direction::Backwards},
         DirectionsData{
             .initial = Direction::Backwards, .prefix_length = 1, .tail = {}})
    .Add(L"SomeForwardsPrefixNoTail",
         {Direction::Forwards, Direction::Forwards, Direction::Forwards,
          Direction::Forwards},
         DirectionsData{
             .initial = Direction::Forwards, .prefix_length = 4, .tail = {}})
    .Add(L"Complex",
         {Direction::Forwards, Direction::Forwards, Direction::Forwards,
          Direction::Backwards, Direction::Backwards, Direction::Forwards,
          Direction::Forwards, Direction::Forwards, Direction::Backwards},
         DirectionsData{.initial = Direction::Forwards,
                        .prefix_length = 3,
                        .tail = {Direction::Backwards, Direction::Backwards,
                                 Direction::Forwards, Direction::Forwards,
                                 Direction::Forwards, Direction::Backwards}});

}  // namespace

futures::Value<CompositeTransformation::Output> Bisect::Apply(
    CompositeTransformation::Input input) const {
  const DirectionsData directions = ProcessDirections(directions_);
  if (directions.prefix_length == 0) return Output{};

  std::variant<Range, LineColumn> range_or_position = directions.ComputeRange(
      input.buffer.contents().snapshot(), input.position, structure_);
  if (auto* position = std::get_if<LineColumn>(&range_or_position); position)
    return CompositeTransformation::Output::SetPosition(*position);

  Range range = std::get<Range>(range_or_position);
  LineColumn center = RangeCenter(range, structure_);
  CompositeTransformation::Output output =
      CompositeTransformation::Output::SetPosition(center);

  static const VisualOverlayPriority kPriority = VisualOverlayPriority(1);
  static const VisualOverlayKey kKey = VisualOverlayKey(L"bisect");
  switch (input.mode) {
    case transformation::Input::Mode::Final:
      break;
    case transformation::Input::Mode::Preview:
      VisualOverlayMap overlays;
      if (range.begin() != center)
        overlays[kPriority][kKey].insert(
            {range.begin(),
             afc::infrastructure::screen::VisualOverlay{
                 .content = SingleLine{LazyString{L"⟦"}},
                 .modifiers = Style{.attributes = StyleAttribute::Reverse}}});
      if (range.end() != center)
        overlays[kPriority][kKey].insert(
            {range.end(),
             afc::infrastructure::screen::VisualOverlay{
                 .content = SingleLine{LazyString{L"⟧"}},
                 .modifiers = Style{.attributes = StyleAttribute::Reverse}}});
      output.Push(VisualOverlay{.visual_overlay_map = std::move(overlays)});
      break;
  }
  return output;
}

}  // namespace afc::editor::transformation
