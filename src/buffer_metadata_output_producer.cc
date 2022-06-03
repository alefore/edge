#include "src/buffer_metadata_output_producer.h"

#include <glog/logging.h>

#include <algorithm>
#include <cctype>
#include <iostream>

#include "src/buffer.h"
#include "src/buffer_variables.h"
#include "src/char_buffer.h"
#include "src/infrastructure/dirname.h"
#include "src/infrastructure/tracker.h"
#include "src/language/hash.h"
#include "src/lazy_string_functional.h"
#include "src/line_marks.h"
#include "src/line_with_cursor.h"
#include "src/parse_tree.h"
#include "src/terminal.h"
#include "src/tests/tests.h"

namespace afc {
namespace editor {
namespace {
using infrastructure::Tracker;
using language::CaptureAndHash;
using language::MakeNonNullShared;
using language::NonNull;
using language::VisitPointer;

namespace gc = language::gc;

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

std::wstring DrawTree(LineNumber line, LineNumberDelta lines_size,
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
  return output;
}

struct MetadataLine {
  wchar_t info_char;
  LineModifier modifier;
  NonNull<std::shared_ptr<const Line>> suffix;
  enum class Type {
    kDefault,
    kMark,
    kFlags,
    kLineContents,
  };
  Type type;
};

ColumnNumberDelta width(const std::wstring prefix, MetadataLine& line) {
  return std::max(ColumnNumberDelta(1), ColumnNumberDelta(prefix.size())) +
         line.suffix->contents()->size();
}

LineWithCursor::Generator NewGenerator(std::wstring input_prefix,
                                       MetadataLine line) {
  return LineWithCursor::Generator::New(CaptureAndHash(
      [](wchar_t info_char, LineModifier modifier, Line suffix,
         std::wstring prefix) {
        Line::Options options;
        if (prefix.empty()) {
          options.AppendCharacter(info_char, {modifier});
        } else {
          options.AppendString(prefix, LineModifierSet{LineModifier::YELLOW});
        }
        options.Append(suffix);
        return LineWithCursor{.line = MakeNonNullShared<Line>(options)};
      },
      line.info_char, line.modifier, std::move(line.suffix.value()),
      std::move(input_prefix)));
}
}  // namespace

LineNumber initial_line(const BufferMetadataOutputOptions& options) {
  CHECK(!options.screen_lines.empty());
  return options.screen_lines.front().range.begin.line;
}

// Assume that the screen is currently showing the screen_position lines out
// of a buffer of size total_size. Map current_line to its associated range of
// lines (for the purposes of the scroll bar). The columns are entirely
// ignored by this function.
Range MapScreenLineToContentsRange(Range lines_shown, LineNumber current_line,
                                   LineNumberDelta total_size) {
  CHECK_GE(current_line, lines_shown.begin.line);
  double buffer_lines_per_screen_line =
      static_cast<double>(total_size.line_delta) /
      (lines_shown.end.line - lines_shown.begin.line).line_delta;
  Range output;
  output.begin.line = LineNumber(
      std::round(buffer_lines_per_screen_line *
                 (current_line - lines_shown.begin.line).line_delta));
  output.end.line = LineNumber(std::round(
      buffer_lines_per_screen_line *
      (current_line + LineNumberDelta(1) - lines_shown.begin.line).line_delta));
  return output;
}

Line ComputeCursorsSuffix(const BufferMetadataOutputOptions& options,
                          LineNumber line) {
  static Tracker tracker(L"BufferMetadataOutput::ComputeCursorsSuffix");
  auto call = tracker.Call();

  const CursorsSet& cursors = options.buffer.active_cursors();
  if (cursors.size() <= 1) {
    return Line(L"");
  }
  CHECK_GE(line, initial_line(options));
  auto range = MapScreenLineToContentsRange(
      Range(LineColumn(LineNumber(initial_line(options))),
            options.screen_lines.back().range.begin),
      line, options.buffer.lines_size());
  int count = 0;
  auto cursors_end = cursors.lower_bound(range.end);
  static const int kStopCount = 10;
  for (auto cursors_it = cursors.lower_bound(range.begin);
       cursors_it != cursors_end && count < kStopCount; ++cursors_it) {
    count++;
    std::distance(cursors_it, cursors_end);
  }

  if (count == 0) {
    return Line(L" ");
  }

  std::wstring output_str = std::wstring(1, L'0' + count);
  LineModifierSet modifiers;
  if (count == kStopCount) {
    output_str = L"+";
    modifiers.insert(LineModifier::BOLD);
  }
  if (range.Contains(*cursors.active())) {
    modifiers.insert(LineModifier::BOLD);
    modifiers.insert(LineModifier::CYAN);
  }
  Line::Options line_options;
  line_options.AppendString(output_str, modifiers);
  return Line(std::move(line_options));
}

GHOST_TYPE_SIZE_T(Rows);

Line ComputeScrollBarSuffix(const BufferMetadataOutputOptions& options,
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
  Rows total_rows = size_t(lines_shown.line_delta) * kRowsPerScreenLine;

  // Number of rows the bar should take.
  Rows bar_size = std::max(
      Rows(1), Rows(std::round(static_cast<double>(total_rows.read()) *
                               static_cast<double>(lines_shown.line_delta) /
                               lines_size.line_delta)));

  // Bar will be shown in lines in interval [start, end] (units are rows).
  Rows start = Rows(std::round(static_cast<double>(total_rows.read()) *
                               static_cast<double>(initial_line(options).line) /
                               lines_size.line_delta));
  Rows end = start + bar_size;

  LineModifierSet modifiers;

  Line::Options line_options;
  Rows current = kRowsPerScreenLine * (line - initial_line(options)).line_delta;

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
            ? LineModifierSet({LineModifier::YELLOW})
            : LineModifierSet({LineModifier::CYAN});

  if (!marks.empty() || !expired_marks.empty()) {
    double buffer_lines_per_row =
        static_cast<double>(options.buffer.lines_size().line_delta) /
        total_rows;
    bool active_marks = false;
    for (size_t row = 0; row < 3; row++) {
      LineColumn begin_line(
          LineNumber((current + row).read() * buffer_lines_per_row));
      LineColumn end_line(
          LineNumber((current + row + 1).read() * buffer_lines_per_row));
      if (begin_line == end_line) continue;
      if (marks.lower_bound(begin_line) != marks.lower_bound(end_line)) {
        active_marks = true;
        base_char |= (1 << (row * 2 + 1));
      } else if (expired_marks.lower_bound(begin_line) !=
                 expired_marks.lower_bound(end_line)) {
        base_char |= (1 << (row * 2 + 1));
      }
    }
    if (active_marks) modifiers = {LineModifier::RED};
  }

  CHECK_LT(base_char, chars.size());
  line_options.AppendString(std::wstring(1, chars[base_char]), modifiers);
  return Line(std::move(line_options));
}

NonNull<std::shared_ptr<Line>> GetDefaultInformation(
    const BufferMetadataOutputOptions& options, LineNumber line) {
  static Tracker tracker(L"BufferMetadataOutput::GetDefaultInformation");
  auto call = tracker.Call();

  Line::Options line_options;

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
  return MakeNonNullShared<Line>(std::move(line_options));
}

template <typename MarkType>
std::list<MarkType> PushMarks(std::multimap<LineColumn, MarkType> input,
                              Range range) {
  static Tracker tracker(L"BufferMetadataOutput::Prepare:PushMarks");
  auto call = tracker.Call();
  std::list<MarkType> output;
  auto it_begin = input.lower_bound(range.begin);
  auto it_end = input.lower_bound(range.end);
  while (it_begin != it_end) {
    if (range.Contains(it_begin->second.target_line_column)) {
      output.push_back(it_begin->second);
    }
    ++it_begin;
  }
  return output;
}

std::list<MetadataLine> Prepare(const BufferMetadataOutputOptions& options,
                                Range range) {
  static Tracker top_tracker(L"BufferMetadataOutput::Prepare");
  auto call = top_tracker.Call();

  std::list<MetadataLine> output;
  const Line& contents = options.buffer.contents().at(range.begin.line).value();
  std::optional<gc::Root<OpenBuffer>> target_buffer_dummy;
  NonNull<const OpenBuffer*> target_buffer =
      NonNull<const OpenBuffer*>::AddressOf(options.buffer);
  if (auto line_buffer = contents.buffer_line_column();
      line_buffer.has_value()) {
    target_buffer_dummy = line_buffer->buffer.Lock();
    if (target_buffer_dummy.has_value())
      target_buffer = NonNull<const OpenBuffer*>::AddressOf(
          target_buffer_dummy->ptr().value());
  }

  auto info_char = L'•';
  auto info_char_modifier = LineModifier::DIM;

  if (target_buffer.get() != &options.buffer) {
    output.push_back(
        MetadataLine{info_char, info_char_modifier,
                     MakeNonNullShared<const Line>(
                         OpenBuffer::FlagsToString(target_buffer->Flags())),
                     MetadataLine::Type::kFlags});
  } else if (contents.modified()) {
    info_char_modifier = LineModifier::GREEN;
    info_char = L'•';
  } else {
    info_char_modifier = LineModifier::DIM;
  }

  VisitPointer(
      contents.metadata(),
      [&output](NonNull<std::shared_ptr<LazyString>> metadata) {
        static Tracker tracker(
            L"BufferMetadataOutput::Prepare:VisitContentsMetadata");
        auto call = tracker.Call();

        if (metadata->size().IsZero()) return;
        ForEachColumn(metadata.value(), [](ColumnNumber, wchar_t c) {
          CHECK(c != L'\n') << "Metadata has invalid newline character.";
        });
        output.push_back(
            MetadataLine{L'>', LineModifier::GREEN,
                         MakeNonNullShared<const Line>(std::move(metadata)),
                         MetadataLine::Type::kLineContents});
      },
      [] {});

  static Tracker tracker_generic_marks_logic(
      L"BufferMetadataOutput::Prepare::GenericMarksLogic");
  auto call_generic_marks_logic = tracker_generic_marks_logic.Call();

  std::list<LineMarks::Mark> marks =
      PushMarks(options.buffer.GetLineMarks(), range);

  std::list<LineMarks::ExpiredMark> expired_marks =
      PushMarks(options.buffer.GetExpiredLineMarks(), range);

  for (const auto& mark : marks) {
    static Tracker tracker(L"BufferMetadataOutput::Prepare:AddMetadataForMark");
    auto call = tracker.Call();
    auto source = options.buffer.editor().buffers()->find(mark.source_buffer);
    output.push_back(MetadataLine{
        output.empty() ? L'!' : L' ',
        output.empty() ? LineModifier::RED : LineModifier::DIM,
        (source != options.buffer.editor().buffers()->end() &&
         mark.source_line <
             LineNumber(0) + source->second.ptr()->contents().size())
            ? source->second.ptr()->contents().at(mark.source_line)
            : MakeNonNullShared<const Line>(L"(dead mark)"),
        MetadataLine::Type::kMark});
  }

  // When an expired mark appears again, no need to show it redundantly (as
  // expired). We use `marks_strings` to detect this.
  std::set<std::wstring> marks_strings;
  for (const auto& mark : marks) {
    if (auto source =
            options.buffer.editor().buffers()->find(mark.source_buffer);
        source != options.buffer.editor().buffers()->end() &&
        mark.source_line <
            LineNumber(0) + source->second.ptr()->contents().size()) {
      marks_strings.insert(
          source->second.ptr()->contents().at(mark.source_line)->ToString());
    }
  }

  for (const auto& mark : expired_marks) {
    static Tracker tracker(
        L"BufferMetadataOutput::Prepare:AddMetadataForExpiredMark");
    auto call = tracker.Call();
    if (auto contents = mark.source_line_content->ToString();
        marks_strings.find(contents) == marks_strings.end()) {
      output.push_back(MetadataLine{'!', LineModifier::RED,
                                    MakeNonNullShared<Line>(L"👻 " + contents),
                                    MetadataLine::Type::kMark});
    }
  }

  if (output.empty()) {
    output.push_back(
        MetadataLine{info_char, info_char_modifier,
                     GetDefaultInformation(options, range.begin.line),
                     MetadataLine::Type::kDefault});
  }
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
  LineNumberDelta sum_sizes;
  for (const auto& b : container) sum_sizes += b.size;
  return sum_sizes;
}

std::list<Box> Squash(std::list<Box> inputs, LineNumberDelta screen_size) {
  LineNumberDelta sum_sizes = SumSizes(inputs);
  if (sum_sizes <= screen_size) return inputs;
  std::vector<Box> boxes(inputs.begin(), inputs.end());
  std::vector<size_t> indices;
  for (size_t i = 0; i < boxes.size(); i++) indices.push_back(i);
  std::sort(indices.begin(), indices.end(), [&](size_t a, size_t b) {
    return boxes[a].size > boxes[b].size ||
           (boxes[a].size == boxes[b].size && b > a);
  });
  CHECK_EQ(indices.size(), boxes.size());
  while (sum_sizes > screen_size) {
    LineNumberDelta current_size = boxes[indices[0]].size;
    for (size_t i = 0;
         i < indices.size() && boxes[indices[i]].size == current_size &&
         sum_sizes > screen_size;
         i++) {
      --boxes[indices[i]].size;
      sum_sizes--;
    }
  }
  return std::list<Box>(boxes.begin(), boxes.end());
}

namespace {
std::list<BoxWithPosition> FindLayout(std::list<Box> boxes,
                                      LineNumberDelta screen_size) {
  boxes = Squash(std::move(boxes), screen_size);
  LineNumberDelta sum_sizes = SumSizes(boxes);
  CHECK_LE(sum_sizes, screen_size);

  std::list<BoxWithPosition> output;
  for (auto box_it = boxes.rbegin(); box_it != boxes.rend(); ++box_it) {
    sum_sizes -= box_it->size;
    LineNumber position = std::max(
        LineNumber() + sum_sizes,
        std::min(box_it->reference, LineNumber() + screen_size - box_it->size));
    output.push_front({.box = *box_it, .position = position});
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
                             auto output = FindLayout(input, size);
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

std::vector<std::wstring> ComputePrefixLines(
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

  std::vector<std::wstring> output(screen_size.line_delta, L"");
  auto get = [&](LineNumber l) -> std::wstring& {
    CHECK_LT(l.line, output.size());
    return output[l.line];
  };
  auto push = [&](LineNumber l, wchar_t c, size_t* indents) {
    std::wstring& target = get(l);
    size_t padding = target.size() < *indents ? *indents - target.size() : 0;
    switch (c) {
      case L'╭':
        target += std::wstring(padding, L' ');
        break;
      case L'╮':
        target += std::wstring(padding, L'─');
        break;
      case L'│':
        target += std::wstring(padding, L' ');
        break;
      case L'╯':
        target += std::wstring(padding, L'─');
        break;
      case L'╰':
        target += std::wstring(padding, L' ');
        break;
    }
    *indents = target.size();
    target.push_back(c);
  };
  for (BoxIndex i = 0; i < boxes.size(); ++i) {
    if (downwards(i)) {
      LineNumber l = boxes[i].position + boxes[i].box.size - LineNumberDelta(1);
      size_t indents = 0;
      push(l, L'╭', &indents);
      ++l;
      while (l < boxes[i].box.reference) {
        push(l, L'│', &indents);
        ++l;
      }
      push(l, L'╯', &indents);
    } else if (upwards(i)) {
      LineNumber l = boxes[i].box.reference;
      size_t indents = 0;
      push(l, L'╮', &indents);
      ++l;
      while (l < boxes[i].position) {
        push(l, L'│', &indents);
        ++l;
      }
      push(l, L'╰', &indents);
    } else {
      size_t indents = 0;
      push(boxes[i].box.reference, L'─', &indents);
    }
  }
  for (const auto& b : boxes) {
    if (b.box.size == LineNumberDelta(1)) continue;
    size_t indents = 0;
    // Figure out the maximum indent.
    for (LineNumberDelta l; l < b.box.size; ++l) {
      indents = std::max(indents, get(b.position + l).size());
    }
    // Add indents for all lines overlapping with the current box.
    for (LineNumberDelta l; l < b.box.size; ++l) {
      std::wstring& target = output[(b.position + l).line];
      CHECK_LE(target.size(), indents);
      target.resize(indents, target.empty() || target.back() == L'╮' ||
                                     target.back() == L'│' ||
                                     target.back() == L'╯'
                                 ? L' '
                                 : L'─');
    }
    // Add the wrappings around the box.
    get(b.position).push_back(b.position >= b.box.reference ? L'┬' : L'╭');
    for (LineNumberDelta l(1); l + LineNumberDelta(1) < b.box.size; ++l) {
      get(b.position + l)
          .push_back(b.position + l == b.box.reference ? L'┤' : L'│');
    }
    get(b.position + b.box.size - LineNumberDelta(1))
        .push_back(b.position + b.box.size - LineNumberDelta(1) <=
                           b.box.reference
                       ? L'┴'
                       : L'╰');
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
    if (Range range = options.screen_lines[i.ToDelta().line_delta].range;
        range.begin.line < LineNumber(0) + options.buffer.lines_size()) {
      metadata_by_line[i.line] = Prepare(options, range);
    }
  }

  std::list<Box> boxes_input;
  for (LineNumber i; i.ToDelta() < screen_size; ++i) {
    const std::list<MetadataLine>& v = metadata_by_line[i.line];
    if (!v.empty() &&
        (v.size() != 1 || v.front().type != MetadataLine::Type::kDefault)) {
      boxes_input.push_back(
          {.reference = i, .size = LineNumberDelta(v.size())});
      VLOG(6) << "Pushed input box: " << boxes_input.back();
    }
  }

  const std::list<BoxWithPosition> boxes_list =
      FindLayout(std::move(boxes_input), screen_size);
  const std::vector<BoxWithPosition> boxes(boxes_list.begin(),
                                           boxes_list.end());

  for (auto& b : boxes) {
    VLOG(5) << "Received output box: " << b;
    CHECK_GE(LineNumberDelta(metadata_by_line[b.box.reference.line].size()),
             b.box.size);
  }

  std::set<LineNumber> lines_referenced;
  for (auto& b : boxes) lines_referenced.insert(b.box.reference);

  const std::vector<std::wstring> prefix_lines =
      ComputePrefixLines(screen_size, boxes);
  ColumnsVector::Column output;
  size_t box_index = 0;
  for (LineNumber i; i.ToDelta() < screen_size; ++i) {
    CHECK_LT(i.line, metadata_by_line.size());
    size_t source;
    if (box_index == boxes.size() || boxes[box_index].position > i) {
      if (metadata_by_line[i.line].empty()) continue;
      CHECK(!metadata_by_line[i.line].empty());
      source = i.line;
    } else {
      source = boxes[box_index].box.reference.line;
      if (metadata_by_line[source].size() == 1) ++box_index;
    }
    CHECK_LT(source, metadata_by_line.size());
    CHECK(!metadata_by_line[source].empty());
    MetadataLine& metadata_line = metadata_by_line[source].front();

    std::wstring prefix = prefix_lines[i.line];
    output.lines.width =
        std::max(output.lines.width, width(prefix, metadata_line));
    output.lines.lines.push_back(
        NewGenerator(std::move(prefix), std::move(metadata_line)));
    output.padding.push_back(
        lines_referenced.find(i) != lines_referenced.end()
            ? ColumnsVector::Padding{.modifiers = {LineModifier::YELLOW},
                                     .head = NewLazyString(L"  ←"),
                                     .body = NewLazyString(L"-")}
            : std::optional<ColumnsVector::Padding>());
    metadata_by_line[source].pop_front();
  }
  return output;
}

}  // namespace editor
}  // namespace afc
