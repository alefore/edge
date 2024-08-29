#include "src/buffer_metadata_output_producer.h"

#include <glog/logging.h>

#include <algorithm>
#include <cctype>
#include <iostream>

#include "src/buffer.h"
#include "src/buffer_registry.h"
#include "src/buffer_variables.h"
#include "src/infrastructure/dirname.h"
#include "src/infrastructure/tracker.h"
#include "src/language/ghost_type_class.h"
#include "src/language/hash.h"
#include "src/language/lazy_string/append.h"
#include "src/language/lazy_string/char_buffer.h"
#include "src/language/lazy_string/functional.h"
#include "src/language/overload.h"
#include "src/line_marks.h"
#include "src/line_with_cursor.h"
#include "src/parse_tree.h"
#include "src/terminal.h"
#include "src/tests/tests.h"

namespace gc = afc::language::gc;
namespace container = afc::language::container;

using afc::infrastructure::Path;
using afc::infrastructure::Tracker;
using afc::infrastructure::screen::CursorsSet;
using afc::infrastructure::screen::LineModifier;
using afc::infrastructure::screen::LineModifierSet;
using afc::language::CaptureAndHash;
using afc::language::GhostType;
using afc::language::IgnoreErrors;
using afc::language::IsError;
using afc::language::MakeNonNullShared;
using afc::language::NonNull;
using afc::language::overload;
using afc::language::ValueOrError;
using afc::language::VisitOptional;
using afc::language::VisitPointer;
using afc::language::lazy_string::ColumnNumber;
using afc::language::lazy_string::ColumnNumberDelta;
using afc::language::lazy_string::Concatenate;
using afc::language::lazy_string::LazyString;
using afc::language::text::Line;
using afc::language::text::LineBuilder;
using afc::language::text::LineColumn;
using afc::language::text::LineMetadataEntry;
using afc::language::text::LineNumber;
using afc::language::text::LineNumberDelta;
using afc::language::text::LineRange;
using afc::language::text::OutgoingLink;
using afc::language::text::Range;

namespace afc {
namespace editor {
namespace {
void Draw(size_t pos, wchar_t padding_char, wchar_t final_char,
          wchar_t connect_final_char, std::wstring& output) {
  CHECK_LT(pos, output.size());
  for (size_t i = 0; i < pos; i++) {
    output[i] = padding_char;
  }
  output[pos] = (pos + 1 == output.size() || output[pos + 1] == L' ' ||
                 output[pos + 1] == L'│')
                    ? final_char
                    : connect_final_char;
}

LazyString DrawTree(LineNumber line, LineNumberDelta lines_size,
                    const ParseTree& root) {
  static Tracker tracker(L"BufferMetadataOutput::DrawTree");
  auto call = tracker.Call();

  // Route along the tree where each child ends after previous line.
  std::vector<const ParseTree*> route_begin;
  if (line > LineNumber(0)) {
    route_begin = MapRoute(
        root, FindRouteToPosition(
                  root, LineColumn(line - LineNumberDelta(1),
                                   std::numeric_limits<ColumnNumber>::max())));
    CHECK(!route_begin.empty() && *route_begin.begin() == &root);
    route_begin.erase(route_begin.begin());
  }

  // Route along the tree where each child ends after current line.
  std::vector<const ParseTree*> route_end;
  if (line < LineNumber(0) + lines_size - LineNumberDelta(1)) {
    route_end = MapRoute(
        root,
        FindRouteToPosition(
            root, LineColumn(line, std::numeric_limits<ColumnNumber>::max())));
    CHECK(!route_end.empty() && *route_end.begin() == &root);
    route_end.erase(route_end.begin());
  }
  std::wstring output(root.depth(), L' ');
  size_t index_begin = 0;
  size_t index_end = 0;
  while (index_begin < route_begin.size() || index_end < route_end.size()) {
    if (index_begin == route_begin.size()) {
      Draw(route_end[index_end]->depth(), L'─', L'╮', L'┬', output);
      index_end++;
      continue;
    }
    if (index_end == route_end.size()) {
      Draw(route_begin[index_begin]->depth(), L'─', L'╯', L'┴', output);
      index_begin++;
      continue;
    }

    if (route_begin[index_begin]->depth() > route_end[index_end]->depth()) {
      Draw(route_begin[index_begin]->depth(), L'─', L'╯', L'┴', output);
      index_begin++;
      continue;
    }

    if (route_end[index_end]->depth() > route_begin[index_begin]->depth()) {
      Draw(route_end[index_end]->depth(), L'─', L'╮', L'┬', output);
      index_end++;
      continue;
    }

    if (route_begin[index_begin] == route_end[index_end]) {
      output[route_begin[index_begin]->depth()] = L'│';
      index_begin++;
      index_end++;
      continue;
    }

    Draw(route_end[index_end]->depth(), L'─', L'┤', L'┼', output);
    index_begin++;
    index_end++;
  }
  // TODO(easy, 2024-07-26): Turn `output` to a LazyString to avoid
  // conversion.
  return LazyString{output};
}

struct MetadataLine {
  wchar_t info_char;
  LineModifier modifier;
  Line suffix;
  enum class Type {
    kDefault,
    kMark,
    kFlags,
    kLineContents,
  };
  Type type;
};

ColumnNumberDelta width(const Line& prefix, MetadataLine& line) {
  return std::max(ColumnNumberDelta(1),
                  ColumnNumberDelta(prefix.contents().size())) +
         line.suffix.contents().size();
}

LineWithCursor::Generator NewGenerator(Line input_prefix, MetadataLine line) {
  return LineWithCursor::Generator::New(CaptureAndHash(
      [](wchar_t info_char, LineModifier modifier, const Line& suffix,
         const Line& prefix) {
        LineBuilder options;
        if (prefix.empty()) {
          options.AppendCharacter(info_char, {modifier});
        } else {
          options.Append(LineBuilder(std::move(prefix)));
        }
        options.Append(LineBuilder(std::move(suffix)));
        return LineWithCursor{.line = std::move(options).Build()};
      },
      line.info_char, line.modifier, std::move(line.suffix),
      std::move(input_prefix)));
}
}  // namespace

LineNumber initial_line(const BufferMetadataOutputOptions& options) {
  CHECK(!options.screen_lines.empty());
  return options.screen_lines.front().range.line();
}

// Assume that the screen is currently showing the screen_position lines out
// of a buffer of size total_size. Map current_line to its associated range of
// lines (for the purposes of the scroll bar). The columns are entirely
// ignored by this function.
Range MapScreenLineToContentsRange(Range lines_shown, LineNumber current_line,
                                   LineNumberDelta total_size) {
  CHECK_GE(current_line, lines_shown.begin().line);
  double buffer_lines_per_screen_line =
      static_cast<double>(total_size.read()) /
      (lines_shown.end().line - lines_shown.begin().line).read();
  return Range(
      LineColumn(LineNumber(
          std::round(buffer_lines_per_screen_line *
                     (current_line - lines_shown.begin().line).read()))),
      LineColumn(LineNumber(std::round(
          buffer_lines_per_screen_line *
          (current_line + LineNumberDelta(1) - lines_shown.begin().line)
              .read()))));
}

LineBuilder ComputeCursorsSuffix(const BufferMetadataOutputOptions& options,
                                 LineNumber line) {
  static Tracker tracker(L"BufferMetadataOutput::ComputeCursorsSuffix");
  auto call = tracker.Call();

  const CursorsSet& cursors = options.buffer.active_cursors();
  if (cursors.size() <= 1) {
    return LineBuilder{};
  }
  CHECK_GE(line, initial_line(options));
  // TODO(2023-12-08, trivial): Switch to LineRange.
  Range range = MapScreenLineToContentsRange(
      Range(LineColumn(LineNumber(initial_line(options))),
            options.screen_lines.back().range.read().begin()),
      line, options.buffer.lines_size());
  int count = 0;
  auto cursors_end = cursors.lower_bound(range.end());
  static const int kStopCount = 10;
  for (auto cursors_it = cursors.lower_bound(range.begin());
       cursors_it != cursors_end && count < kStopCount; ++cursors_it) {
    count++;
  }

  if (count == 0) return LineBuilder{LazyString{L" "}};

  LazyString output_str{ColumnNumberDelta{1}, L'0' + count};
  LineModifierSet modifiers;
  if (count == kStopCount) {
    output_str = LazyString{L"+"};
    modifiers.insert(LineModifier::kBold);
  }
  if (range.Contains(*cursors.active())) {
    modifiers.insert(LineModifier::kBold);
    modifiers.insert(LineModifier::kCyan);
  }
  LineBuilder line_options;
  line_options.AppendString(output_str, modifiers);
  return line_options;
}

class Rows : public GhostType<Rows, size_t> {};

LineBuilder ComputeScrollBarSuffix(const BufferMetadataOutputOptions& options,
                                   LineNumber line) {
  static Tracker tracker(L"BufferMetadataOutput::ComputeScrollBarSuffix");
  auto call = tracker.Call();

  LineNumberDelta lines_size = options.buffer.lines_size();
  LineNumberDelta lines_shown = LineNumberDelta(options.screen_lines.size());
  DCHECK_GE(line, initial_line(options));
  DCHECK_LE(line - initial_line(options), lines_shown)
      << "Line is " << line << " and view_start is " << initial_line(options)
      << ", which exceeds lines_shown_ of " << lines_shown;
  DCHECK_LT(initial_line(options), LineNumber(0) + lines_size);

  static const Rows kRowsPerScreenLine(3);
  Rows total_rows = size_t(lines_shown.read()) * kRowsPerScreenLine;

  // Number of rows the bar should take.
  Rows bar_size = std::max(
      Rows(1), Rows(std::round(static_cast<double>(total_rows.read()) *
                               static_cast<double>(lines_shown.read()) /
                               lines_size.read())));

  // Bar will be shown in lines in interval [start, end] (units are rows).
  Rows start = Rows(std::round(
      static_cast<double>(total_rows.read()) *
      static_cast<double>(initial_line(options).read()) / lines_size.read()));
  Rows end = start + bar_size;

  LineModifierSet modifiers;

  LineBuilder line_options;
  DCHECK_GE(line, initial_line(options));
  Rows current = kRowsPerScreenLine *
                 static_cast<size_t>((line - initial_line(options)).read());

  // Characters:
  // 01
  // 23
  // 45
  static const std::wstring chars =
      L" 🬀🬁🬂"
      L"🬃🬄🬅🬆"
      L"🬇🬈🬉🬊"
      L"🬋🬌🬍🬎"
      L"🬏🬐🬑🬒"
      L"🬓▌🬔🬕"
      L"🬖🬗🬘🬙"
      L"🬚🬛🬜🬝"
      L"🬞🬟🬠🬡"
      L"🬢🬣🬤🬥"
      L"🬦🬧▐🬨"
      L"🬩🬪🬫🬬"
      L"🬭🬮🬯🬰"
      L"🬱🬲🬳🬴"
      L"🬵🬶🬷🬸"
      L"🬹🬺🬻█";

  size_t base_char = 0;

  const std::multimap<LineColumn, LineMarks::Mark>& marks =
      options.buffer.GetLineMarks();
  const std::multimap<LineColumn, LineMarks::ExpiredMark>& expired_marks =
      options.buffer.GetExpiredLineMarks();
  for (size_t row = 0; row < 3; row++)
    if (current + row >= start && current + row < end) {
      base_char |= 1 << (row * 2);
      if (marks.empty() && expired_marks.empty())
        base_char |= 1 << (row * 2 + 1);
    }
  if (base_char != 0)
    modifiers =
        MapScreenLineToContentsRange(
            Range(LineColumn(LineNumber(initial_line(options))),
                  LineColumn(LineNumber(initial_line(options) + lines_shown))),
            line, options.buffer.lines_size())
                .Contains(options.buffer.position())
            ? LineModifierSet({LineModifier::kYellow})
            : LineModifierSet({LineModifier::kCyan});

  if (!marks.empty() || !expired_marks.empty()) {
    double buffer_lines_per_row =
        static_cast<double>(options.buffer.lines_size().read()) /
        static_cast<double>(total_rows.read());
    bool active_marks = false;
    for (size_t row = 0; row < 3; row++) {
      LineColumn begin_line(
          LineNumber((current + row).read() * buffer_lines_per_row));
      LineColumn end_line(
          LineNumber((current + row + 1ul).read() * buffer_lines_per_row));
      if (begin_line == end_line) continue;
      if (marks.lower_bound(begin_line) != marks.lower_bound(end_line)) {
        active_marks = true;
        base_char |= (1 << (row * 2 + 1));
      } else if (expired_marks.lower_bound(begin_line) !=
                 expired_marks.lower_bound(end_line)) {
        base_char |= (1 << (row * 2 + 1));
      }
    }
    if (active_marks) modifiers = {LineModifier::kRed};
  }

  CHECK_LT(base_char, chars.size());
  line_options.AppendString(LazyString{ColumnNumberDelta{1}, chars[base_char]},
                            modifiers);
  return line_options;
}

Line GetDefaultInformation(const BufferMetadataOutputOptions& options,
                           LineNumber line) {
  static Tracker tracker(L"BufferMetadataOutput::GetDefaultInformation");
  auto call = tracker.Call();

  LineBuilder line_options;

  auto parse_tree = options.buffer.simplified_parse_tree();
  line_options.AppendString(
      DrawTree(line, options.buffer.lines_size(), parse_tree.value()),
      std::nullopt);

  if (options.buffer.lines_size() >
      LineNumberDelta(options.screen_lines.size())) {
    if (options.buffer.Read(buffer_variables::scrollbar)) {
      CHECK_GE(line, initial_line(options));
      line_options.Append(ComputeCursorsSuffix(options, line));
      line_options.Append(ComputeScrollBarSuffix(options, line));
    }
    if (!options.zoomed_out_tree.children().empty()) {
      line_options.AppendString(
          DrawTree(line - initial_line(options).ToDelta(),
                   LineNumberDelta(options.screen_lines.size()),
                   options.zoomed_out_tree),
          std::nullopt);
    }
  }
  return std::move(line_options).Build();
}

template <typename MarkType>
std::list<MarkType> PushMarks(std::multimap<LineColumn, MarkType> input,
                              Range range) {
  TRACK_OPERATION(BufferMetadataOutput_Prepare_PushMarks);
  return container::Materialize<std::list<MarkType>>(
      std::ranges::subrange(input.lower_bound(range.begin()),
                            input.lower_bound(range.end())) |
      std::views::values | std::views::filter([&range](const MarkType& m) {
        return range.Contains(m.target_line_column);
      }));
}

std::list<MetadataLine> Prepare(const BufferMetadataOutputOptions& options,
                                LineRange range) {
  static Tracker top_tracker(L"BufferMetadataOutput::Prepare");
  auto top_call = top_tracker.Call();

  std::list<MetadataLine> output;
  const Line& contents = options.buffer.contents().at(range.line());
  std::optional<gc::Root<OpenBuffer>> target_buffer_dummy;
  NonNull<const OpenBuffer*> target_buffer =
      NonNull<const OpenBuffer*>::AddressOf(options.buffer);
  VisitPointer(
      contents.outgoing_link(),
      [&](const OutgoingLink& link) {
        VisitOptional(
            [&](gc::Root<OpenBuffer> target_buffer_dummy_value) {
              target_buffer_dummy = std::move(target_buffer_dummy_value);
              target_buffer = NonNull<const OpenBuffer*>::AddressOf(
                  target_buffer_dummy->ptr().value());
            },
            [] {},
            options.buffer.editor().buffer_registry().FindPath(link.path));
      },
      [] {});
  auto info_char = L'•';
  auto info_char_modifier = LineModifier::kDim;

  if (target_buffer.get() != &options.buffer) {
    output.push_back(MetadataLine{
        info_char, info_char_modifier,
        LineBuilder(OpenBuffer::FlagsToString(target_buffer->Flags())).Build(),
        MetadataLine::Type::kFlags});
#if 0
  } else if (contents.modified()) {
    info_char_modifier = LineModifier::kGreen;
    info_char = L'•';
#endif
  } else {
    info_char_modifier = LineModifier::kDim;
  }

  if (LazyString metadata = Concatenate(
          std::views::transform(
              contents.metadata(),
              [](const std::pair<LazyString, LineMetadataEntry>& item) {
                static Tracker tracker(
                    L"BufferMetadataOutput::Prepare:VisitContentsMetadata");
                auto call = tracker.Call();
                return item.first +
                       (item.first.IsEmpty() ? LazyString{}
                                             : LazyString{L":"}) +
                       item.second.get_value();
              }) |
          std::views::filter([](const LazyString& a) { return !a.IsEmpty(); }));
      !metadata.IsEmpty())
    output.push_back(MetadataLine{L'>', LineModifier::kGreen,
                                  LineBuilder(metadata).Build(),
                                  MetadataLine::Type::kLineContents});

  static Tracker tracker_generic_marks_logic(
      L"BufferMetadataOutput::Prepare::GenericMarksLogic");
  auto call_generic_marks_logic = tracker_generic_marks_logic.Call();

  std::list<LineMarks::Mark> marks =
      PushMarks(options.buffer.GetLineMarks(), range.read());

  std::list<LineMarks::ExpiredMark> expired_marks =
      PushMarks(options.buffer.GetExpiredLineMarks(), range.read());

  for (const auto& mark : marks) {
    TRACK_OPERATION(BufferMetadataOutput_Prepare_AddMetadataForMark);
    const std::optional<gc::Root<OpenBuffer>> source =
        options.buffer.editor().buffer_registry().Find(mark.source_buffer);
    output.push_back(MetadataLine{
        output.empty() ? L'!' : L' ',
        output.empty() ? LineModifier::kRed : LineModifier::kDim,
        (source.has_value() &&
         mark.source_line < LineNumber(0) + source->ptr()->contents().size())
            ? source->ptr()->contents().at(mark.source_line)
            : Line(LazyString{L"(dead mark: "} +
                   ToLazyString(mark.source_buffer) + LazyString{L")"}),
        MetadataLine::Type::kMark});
  }

  // When an expired mark appears again, no need to show it redundantly (as
  // expired). We use `marks_strings` to detect this.
  // TODO(easy, 2023-12-14): This should use Line directly?
  std::set<LazyString> marks_strings;
  for (const auto& mark : marks)
    VisitOptional(
        [&](gc::Root<OpenBuffer> source) {
          if (mark.source_line <
              LineNumber(0) + source.ptr()->contents().size()) {
            marks_strings.insert(
                source.ptr()->contents().at(mark.source_line).contents());
          }
        },
        [] {},
        options.buffer.editor().buffer_registry().Find(mark.source_buffer));

  for (const auto& mark : expired_marks) {
    static Tracker tracker(
        L"BufferMetadataOutput::Prepare:AddMetadataForExpiredMark");
    auto call = tracker.Call();
    if (const Line& mark_contents = mark.source_line_content;
        !marks_strings.contains(mark_contents.contents())) {
      LineBuilder wrapper(LazyString{L"👻 "});
      wrapper.Append(LineBuilder(mark_contents));
      output.push_back(MetadataLine{'!', LineModifier::kRed,
                                    std::move(wrapper).Build(),
                                    MetadataLine::Type::kMark});
    }
  }

  if (output.empty())
    output.push_back(MetadataLine{info_char, info_char_modifier,
                                  GetDefaultInformation(options, range.line()),
                                  MetadataLine::Type::kDefault});
  CHECK(!output.empty());
  return output;
}

struct Box {
  const LineNumber reference;
  LineNumberDelta size;

  bool operator==(const Box& other) const {
    return reference == other.reference && size == other.size;
  }
};

std::ostream& operator<<(std::ostream& os, const Box& b) {
  os << "[box: reference:" << b.reference << ", size:" << b.size << "]";
  return os;
}

struct BoxWithPosition {
  Box box;
  LineNumber position;
  LineModifierSet modifiers = {};

  bool operator==(const BoxWithPosition& other) const {
    return box == other.box && position == other.position;
  }
};

std::ostream& operator<<(std::ostream& os, const BoxWithPosition& b) {
  os << "[box-with-position: box:" << b.box << ", position:" << b.position
     << "]";
  return os;
}

template <typename Container>
LineNumberDelta SumSizes(const Container& container) {
  return container::Sum(
      LineNumberDelta(),
      container | std::views::transform([](auto value) { return value.size; }));
}

std::list<Box> Squash(std::list<Box> inputs, LineNumberDelta screen_size) {
  LineNumberDelta sum_sizes = SumSizes(inputs);
  if (sum_sizes <= screen_size) return inputs;
  std::vector<Box> boxes(inputs.begin(), inputs.end());
  std::vector<size_t> indices;
  VLOG(6) << "Pushing indices.";
  for (size_t i = 0; i < boxes.size(); i++) indices.push_back(i);
  VLOG(6) << "Sorting indices.";
  std::sort(indices.begin(), indices.end(), [&](size_t a, size_t b) {
    return boxes[a].size > boxes[b].size ||
           (boxes[a].size == boxes[b].size && b > a);
  });
  CHECK_EQ(indices.size(), boxes.size());
  VLOG(6) << "Starting reduction.";
  while (sum_sizes > screen_size) {
    LineNumberDelta current_size = boxes[indices[0]].size;
    for (size_t i = 0;
         i < indices.size() && boxes[indices[i]].size == current_size &&
         sum_sizes > screen_size;
         i++) {
      VLOG(7) << "Shrinking box: " << indices[i];
      --boxes[indices[i]].size;
      --sum_sizes;
    }
  }
  return std::list<Box>(boxes.begin(), boxes.end());
}

namespace {
std::list<BoxWithPosition> FindLayout(std::list<Box> boxes,
                                      LineNumberDelta screen_size) {
  VLOG(4) << "Calling Squash: " << boxes.size();
  boxes = Squash(std::move(boxes), screen_size);
  VLOG(4) << "Sum sizes: " << boxes.size();
  LineNumberDelta sum_sizes = SumSizes(boxes);
  CHECK_LE(sum_sizes, screen_size);

  std::list<BoxWithPosition> output;
  size_t count = 0;
  VLOG(5) << "Adjusting boxes.";
  for (const auto& box : boxes | std::views::reverse) {
    sum_sizes -= box.size;
    LineNumber position = std::max(
        LineNumber() + sum_sizes,
        std::min(box.reference, LineNumber() + screen_size - box.size));
    static const std::vector<LineModifierSet> modifiers = {
        LineModifierSet{LineModifier::kYellow},
        LineModifierSet{LineModifier::kCyan},
        LineModifierSet{LineModifier::kGreen},
        LineModifierSet{LineModifier::kBlue},
    };
    output.push_front(
        BoxWithPosition{.box = box,
                        .position = position,
                        .modifiers = modifiers[count++ % modifiers.size()]});
    screen_size = position.ToDelta();
  }
  return output;
}

bool find_layout_tests_registration =
    tests::Register(L"BufferMetadataOutput::FindLayout", [] {
      auto SimpleTest = [](std::wstring name, std::list<Box> input,
                           LineNumberDelta size,
                           std::list<BoxWithPosition> expected_output) {
        return tests::Test{.name = name, .callback = [=] {
                             LOG(INFO) << "Finding layout...";
                             auto output = FindLayout(input, size);
                             LOG(INFO) << "Found layout: " << output.size();
                             CHECK_EQ(output.size(), expected_output.size());
                             auto it_output = output.begin();
                             auto it_expected_output = expected_output.begin();
                             while (it_output != output.end()) {
                               CHECK_EQ(*it_output, *it_expected_output);
                               ++it_output;
                               ++it_expected_output;
                             }
                           }};
      };
      return std::vector<tests::Test>({
          SimpleTest(L"Empty", {}, LineNumberDelta(10), {}),
          SimpleTest(L"SingleSmallBox",
                     {{.reference = LineNumber(3), .size = LineNumberDelta(2)}},
                     LineNumberDelta(10),
                     {BoxWithPosition{.box = {.reference = LineNumber(3),
                                              .size = LineNumberDelta(2)},
                                      .position = LineNumber(3)}}),
          SimpleTest(L"BoxPushedUp",
                     {{.reference = LineNumber(8), .size = LineNumberDelta(6)}},
                     LineNumberDelta(10),
                     {BoxWithPosition{.box = {.reference = LineNumber(8),
                                              .size = LineNumberDelta(6)},
                                      .position = LineNumber(4)}}),
          SimpleTest(
              L"SingleLargeBox",
              {{.reference = LineNumber(8), .size = LineNumberDelta(120)}},
              LineNumberDelta(10),
              {BoxWithPosition{.box = {.reference = LineNumber(8),
                                       .size = LineNumberDelta(10)},
                               .position = LineNumber(0)}}),
          SimpleTest(
              L"Squash",
              {{.reference = LineNumber(3), .size = LineNumberDelta(100)},
               {.reference = LineNumber(4), .size = LineNumberDelta(2)},
               {.reference = LineNumber(7), .size = LineNumberDelta(8)}},
              LineNumberDelta(10),
              {BoxWithPosition{.box = {.reference = LineNumber(3),
                                       .size = LineNumberDelta(4)},
                               .position = LineNumber(0)},
               BoxWithPosition{.box = {.reference = LineNumber(4),
                                       .size = LineNumberDelta(2)},
                               .position = LineNumber(4)},
               BoxWithPosition{.box = {.reference = LineNumber(7),
                                       .size = LineNumberDelta(4)},
                               .position = LineNumber(6)}}),
          SimpleTest(L"CascadingUpwards",
                     {{.reference = LineNumber(2), .size = LineNumberDelta(2)},
                      {.reference = LineNumber(4), .size = LineNumberDelta(2)},
                      {.reference = LineNumber(6), .size = LineNumberDelta(2)},
                      {.reference = LineNumber(8), .size = LineNumberDelta(3)}},
                     LineNumberDelta(10),
                     {BoxWithPosition{.box = {.reference = LineNumber(2),
                                              .size = LineNumberDelta(2)},
                                      .position = LineNumber(1)},
                      BoxWithPosition{.box = {.reference = LineNumber(4),
                                              .size = LineNumberDelta(2)},
                                      .position = LineNumber(3)},
                      BoxWithPosition{.box = {.reference = LineNumber(6),
                                              .size = LineNumberDelta(2)},
                                      .position = LineNumber(5)},
                      BoxWithPosition{.box = {.reference = LineNumber(8),
                                              .size = LineNumberDelta(3)},
                                      .position = LineNumber(7)}}),
      });
    }());

std::vector<LineBuilder> ComputePrefixLines(
    LineNumberDelta screen_size, const std::vector<BoxWithPosition>& boxes) {
  using BoxIndex = size_t;
  auto downwards = [&](BoxIndex i) {
    const BoxWithPosition& box = boxes[i];
    return box.box.reference >= boxes[i].position + boxes[i].box.size;
  };
  auto upwards = [&](BoxIndex i) {
    const BoxWithPosition& box = boxes[i];
    return box.box.reference < boxes[i].position;
  };

  // We group them into consecutive sub-groups such that each subgroup starts
  // with a section of non-upwards boxes, followed by a section of consecutive
  // upward boxes. We represent each group simply by remembering its first
  // index.
  std::vector<BoxIndex> box_groups;
  {
    BoxIndex index = 0;
    while (index < boxes.size()) {
      box_groups.push_back(index);
      while (index < boxes.size() && !upwards(index)) index++;
      while (index < boxes.size() && upwards(index)) index++;
    }
  }

  std::vector<LineBuilder> output = container::MaterializeVector(
      std::views::iota(0, screen_size.read()) |
      std::views::transform([](auto) { return LineBuilder(); }));
  auto get = [&](LineNumber l) -> LineBuilder& {
    CHECK_LT(l.read(), output.size());
    return output.at(l.read());
  };
  for (BoxIndex start : box_groups) {
    BoxIndex index = start;
    auto push = [&](LineNumber l, wchar_t c, ColumnNumberDelta* indents) {
      bool padding_dash = false;
      switch (c) {
        case L'╮':
        case L'╯':
        case L'─':
          padding_dash = true;
          break;
        case L'╭':
        case L'│':
        case L'╰':
          break;
        default:
          LOG(FATAL) << "Unexpected character: " << static_cast<int>(c);
      }
      LineBuilder& target = get(l);
      ColumnNumberDelta padding_size = target.size() < *indents
                                           ? *indents - target.size()
                                           : ColumnNumberDelta();
      target.AppendString(LazyString{padding_size, padding_dash ? L'─' : L' '} +
                              LazyString{ColumnNumberDelta{1}, c},
                          boxes[index].modifiers);
      *indents = target.size() - ColumnNumberDelta(1);
    };

    VLOG(5) << "Inserting lines for !upwards boxes at section: " << index;
    while (index < boxes.size() && !upwards(index)) {
      if (downwards(index)) {
        LineNumber l =
            boxes[index].position + boxes[index].box.size - LineNumberDelta(1);
        ColumnNumberDelta indents;
        push(l, L'╭', &indents);
        ++l;
        while (l < boxes[index].box.reference) {
          push(l, L'│', &indents);
          ++l;
        }
        push(l, L'╯', &indents);
      } else {
        ColumnNumberDelta indents;
        push(boxes[index].box.reference, L'─', &indents);
      }
      index++;
    }

    if (index < boxes.size()) {
      VLOG(5)
          << "Detecting end of consecutive set of upwards boxes starting at: "
          << index;
      const BoxIndex first_upwards = index;
      while (index + 1 < boxes.size() && upwards(index + 1)) index++;
      CHECK_GE(index, first_upwards);

      VLOG(5) << "Adding lines for upwards boxes in reverse order, from "
              << index << " to " << first_upwards;
      for (; index >= first_upwards; index--) {
        LineNumber l = boxes[index].position;
        ColumnNumberDelta indents;
        push(l, L'╰', &indents);
        --l;
        while (l > boxes[index].box.reference) {
          push(l, L'│', &indents);
          --l;
        }
        push(l, L'╮', &indents);
        ++l;
      }
    }
  }
  for (const auto& b : boxes) {
    if (b.box.size == LineNumberDelta(1)) continue;
    ColumnNumberDelta indents;
    // Figure out the maximum indent.
    for (LineNumberDelta l; l < b.box.size; ++l) {
      indents = std::max(indents, get(b.position + l).size());
    }
    // Add indents for all lines overlapping with the current box.
    for (LineNumberDelta l; l < b.box.size; ++l) {
      LineBuilder& target = output[(b.position + l).read()];
      CHECK_LE(target.size(), indents);
      target.AppendString(
          LazyString{
              indents - target.size(),
              target.size().IsZero() ||
                      std::wstring(L"╮│╯").find_first_of(target.contents().get(
                          ColumnNumber() + target.contents().size() -
                          ColumnNumberDelta(1))) != std::wstring::npos
                  ? L' '
                  : L'─'},
          b.modifiers);
    }
    // Add the wrappings around the box.
    get(b.position)
        .AppendCharacter(b.position >= b.box.reference ? L'┬' : L'╭',
                         b.modifiers);
    for (LineNumberDelta l(1); l + LineNumberDelta(1) < b.box.size; ++l) {
      get(b.position + l)
          .AppendCharacter(b.position + l == b.box.reference ? L'┤' : L'│',
                           b.modifiers);
    }
    get(b.position + b.box.size - LineNumberDelta(1))
        .AppendCharacter(
            b.position + b.box.size - LineNumberDelta(1) <= b.box.reference
                ? L'┴'
                : L'╰',
            b.modifiers);
  }
  return output;
}
}  // namespace

ColumnsVector::Column BufferMetadataOutput(
    BufferMetadataOutputOptions options) {
  static Tracker tracker(L"BufferMetadataOutput");
  auto call = tracker.Call();

  const LineNumberDelta screen_size(options.screen_lines.size());
  if (screen_size.IsZero()) return {};

  std::vector<std::list<MetadataLine>> metadata_by_line(
      options.screen_lines.size());
  for (LineNumber i; i.ToDelta() < screen_size; ++i) {
    if (LineRange range = options.screen_lines[i.ToDelta().read()].range;
        range.line() < LineNumber(0) + options.buffer.lines_size()) {
      metadata_by_line[i.read()] = Prepare(options, range);
    }
  }

  std::list<Box> boxes_input;
  for (LineNumber i; i.ToDelta() < screen_size; ++i) {
    const std::list<MetadataLine>& v = metadata_by_line[i.read()];
    if (!v.empty() &&
        (v.size() != 1 || v.front().type != MetadataLine::Type::kDefault)) {
      boxes_input.push_back(
          {.reference = i, .size = LineNumberDelta(v.size())});
      VLOG(6) << "Pushed input box: " << boxes_input.back();
    }
  }

  const std::vector<BoxWithPosition> boxes = container::MaterializeVector(
      FindLayout(std::move(boxes_input), screen_size));

  for (auto& b : boxes) {
    VLOG(5) << "Received output box: " << b;
    CHECK_GE(LineNumberDelta(metadata_by_line[b.box.reference.read()].size()),
             b.box.size);
  }

  std::set<LineNumber> lines_referenced = container::MaterializeSet(
      boxes | std::views::transform([](auto& b) { return b.box.reference; }));

  std::vector<LineBuilder> prefix_lines =
      ComputePrefixLines(screen_size, boxes);
  ColumnsVector::Column output;
  size_t box_index = 0;
  for (LineNumber i; i.ToDelta() < screen_size; ++i) {
    CHECK_LT(i.read(), metadata_by_line.size());
    size_t source;
    if (box_index == boxes.size() || boxes[box_index].position > i) {
      if (metadata_by_line[i.read()].empty()) continue;
      CHECK(!metadata_by_line[i.read()].empty());
      source = i.read();
    } else {
      source = boxes[box_index].box.reference.read();
      if (metadata_by_line[source].size() == 1) ++box_index;
    }
    CHECK_LT(source, metadata_by_line.size());
    CHECK(!metadata_by_line[source].empty());
    MetadataLine& metadata_line = metadata_by_line[source].front();

    Line prefix = std::move(prefix_lines[i.read()]).Build();
    output.lines.width =
        std::max(output.lines.width, width(prefix, metadata_line));
    output.lines.lines.push_back(
        NewGenerator(std::move(prefix), std::move(metadata_line)));
    output.padding.push_back(
        lines_referenced.contains(i)
            ? ColumnsVector::Padding{.modifiers = {LineModifier::kYellow},
                                     .head = LazyString{L"  ←"},
                                     .body = LazyString{L"-"}}
            : std::optional<ColumnsVector::Padding>{});
    metadata_by_line[source].pop_front();
  }
  return output;
}

}  // namespace editor
}  // namespace afc
