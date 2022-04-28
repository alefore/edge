#include "src/buffer_metadata_output_producer.h"

#include <glog/logging.h>

#include <algorithm>
#include <cctype>
#include <iostream>

#include "src/buffer.h"
#include "src/buffer_variables.h"
#include "src/char_buffer.h"
#include "src/infrastructure/dirname.h"
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
using language::CaptureAndHash;
using language::MakeNonNullShared;
using language::NonNull;
using language::VisitPointer;

void Draw(size_t pos, wchar_t padding_char, wchar_t final_char,
          wchar_t connect_final_char, wstring& output) {
  CHECK_LT(pos, output.size());
  for (size_t i = 0; i < pos; i++) {
    output[i] = padding_char;
  }
  output[pos] = (pos + 1 == output.size() || output[pos + 1] == L' ' ||
                 output[pos + 1] == L'│')
                    ? final_char
                    : connect_final_char;
}

wstring DrawTree(LineNumber line, LineNumberDelta lines_size,
                 const ParseTree& root) {
  // Route along the tree where each child ends after previous line.
  vector<const ParseTree*> route_begin;
  if (line > LineNumber(0)) {
    route_begin = MapRoute(
        root, FindRouteToPosition(
                  root, LineColumn(line - LineNumberDelta(1),
                                   std::numeric_limits<ColumnNumber>::max())));
    CHECK(!route_begin.empty() && *route_begin.begin() == &root);
    route_begin.erase(route_begin.begin());
  }

  // Route along the tree where each child ends after current line.
  vector<const ParseTree*> route_end;
  if (line < LineNumber(0) + lines_size - LineNumberDelta(1)) {
    route_end = MapRoute(
        root,
        FindRouteToPosition(
            root, LineColumn(line, std::numeric_limits<ColumnNumber>::max())));
    CHECK(!route_end.empty() && *route_end.begin() == &root);
    route_end.erase(route_end.begin());
  }
  wstring output(root.depth(), L' ');
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
  return max(ColumnNumberDelta(1), ColumnNumberDelta(prefix.size())) +
         line.suffix->contents()->size();
}

LineWithCursor::Generator NewGenerator(std::wstring prefix, MetadataLine line) {
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
      line.info_char, line.modifier, std::move(*line.suffix),
      std::move(prefix)));
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

Line ComputeMarksSuffix(const BufferMetadataOutputOptions& options,
                        LineNumber line) {
  CHECK_GE(line, initial_line(options));
  const std::multimap<size_t, LineMarks::Mark>& marks =
      options.buffer.GetLineMarks();
  if (marks.empty()) return Line(L"");
  auto range = MapScreenLineToContentsRange(
      Range(LineColumn(LineNumber(initial_line(options))),
            options.screen_lines.back().range.begin),
      line, options.buffer.lines_size());

  auto begin = marks.lower_bound(range.begin.line.line);
  auto end = marks.lower_bound(range.end.line.line);
  if (begin == end) return Line(L" ");
  LineModifierSet modifiers;
  for (auto it = begin; it != end && modifiers.empty(); ++it)
    if (!it->second.IsExpired()) modifiers.insert(LineModifier::RED);
  Line::Options line_options;
  line_options.AppendString(L"!", modifiers);
  return Line(std::move(line_options));
}

Line ComputeCursorsSuffix(const BufferMetadataOutputOptions& options,
                          LineNumber line) {
  auto cursors = options.buffer.active_cursors();
  if (cursors->size() <= 1) {
    return Line(L"");
  }
  CHECK_GE(line, initial_line(options));
  auto range = MapScreenLineToContentsRange(
      Range(LineColumn(LineNumber(initial_line(options))),
            options.screen_lines.back().range.begin),
      line, options.buffer.lines_size());
  int count = 0;
  auto cursors_end = cursors->lower_bound(range.end);
  static const int kStopCount = 10;
  for (auto cursors_it = cursors->lower_bound(range.begin);
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
  if (range.Contains(*cursors->active())) {
    modifiers.insert(LineModifier::BOLD);
    modifiers.insert(LineModifier::CYAN);
  }
  Line::Options line_options;
  line_options.AppendString(output_str, modifiers);
  return Line(std::move(line_options));
}

Line ComputeScrollBarSuffix(const BufferMetadataOutputOptions& options,
                            LineNumber line) {
  LineNumberDelta lines_size = options.buffer.lines_size();
  LineNumberDelta lines_shown = LineNumberDelta(options.screen_lines.size());
  DCHECK_GE(line, initial_line(options));
  DCHECK_LE(line - initial_line(options), lines_shown)
      << "Line is " << line << " and view_start is " << initial_line(options)
      << ", which exceeds lines_shown_ of " << lines_shown;
  DCHECK_LT(initial_line(options), LineNumber(0) + lines_size);

  using Rows = size_t;  // TODO(easy, 2022-04-28): Use a ghost type.

  static const Rows kRowsPerScreenLine = 4;
  Rows total_rows = lines_shown.line_delta * kRowsPerScreenLine;

  // Number of rows the bar should take.
  Rows bar_size = max(
      size_t(1), size_t(std::round(total_rows *
                                   static_cast<double>(lines_shown.line_delta) /
                                   lines_size.line_delta)));

  // Bar will be shown in lines in interval [start, end] (units are rows).
  Rows start =
      std::round(total_rows * static_cast<double>(initial_line(options).line) /
                 lines_size.line_delta);
  Rows end = start + bar_size;

  LineModifierSet modifiers =
      MapScreenLineToContentsRange(
          Range(LineColumn(LineNumber(initial_line(options))),
                LineColumn(LineNumber(initial_line(options) + lines_shown))),
          line, options.buffer.lines_size())
              .Contains(options.buffer.position())
          ? LineModifierSet({LineModifier::YELLOW})
          : LineModifierSet({LineModifier::CYAN});

  Line::Options line_options;
  Rows current = kRowsPerScreenLine * (line - initial_line(options)).line_delta;

  wchar_t base_char = L'⠀';

  auto turn_on = [&base_char](size_t row, bool left, bool right) {
    // Braille characters:
    // 14
    // 25
    // 36
    // 78
    CHECK_LT(row, 4ul);
    switch (row) {
      case 0:
        return (left ? 0x01 : 0) + (right ? 0x08 : 0);
      case 1:
        return (left ? 0x02 : 0) + (right ? 0x10 : 0);
      case 2:
        return (left ? 0x04 : 0) + (right ? 0x20 : 0);
      case 3:
        return (left ? 0x40 : 0) + (right ? 0x80 : 0);
    }
    LOG(FATAL) << "Unhandled switch case.";
    return 0;
  };

  const std::multimap<size_t, LineMarks::Mark>& marks =
      options.buffer.GetLineMarks();
  for (size_t row = 0; row < 4; row++)
    if (current + row >= start && current + row < end)
      base_char += turn_on(row, true, marks.empty());

  if (!marks.empty()) {
    double buffer_lines_per_row =
        static_cast<double>(options.buffer.lines_size().line_delta) /
        total_rows;
    bool active_marks = false;
    for (size_t row = 0; row < 4; row++) {
      size_t begin_line = (current + row) * buffer_lines_per_row;
      size_t end_line = (current + row + 1) * buffer_lines_per_row;
      if (begin_line == end_line) continue;
      auto begin = marks.lower_bound(begin_line);
      auto end = marks.lower_bound(end_line);
      if (begin != end) {
        for (auto it = begin; it != end && !active_marks; ++it)
          if (!it->second.IsExpired()) active_marks = true;
        base_char += turn_on(row, false, true);
      }
    }
    if (active_marks) modifiers = {LineModifier::RED};
  }

  line_options.AppendString(std::wstring(1, base_char), modifiers);
  return Line(std::move(line_options));
}

NonNull<std::shared_ptr<Line>> GetDefaultInformation(
    const BufferMetadataOutputOptions& options, LineNumber line) {
  Line::Options line_options;
  auto parse_tree = options.buffer.simplified_parse_tree();
  line_options.AppendString(
      DrawTree(line, options.buffer.lines_size(), *parse_tree), std::nullopt);

  if (options.buffer.lines_size() >
      LineNumberDelta(options.screen_lines.size())) {
    if (options.buffer.Read(buffer_variables::scrollbar)) {
      CHECK_GE(line, initial_line(options));
      line_options.Append(ComputeCursorsSuffix(options, line));
      line_options.Append(ComputeMarksSuffix(options, line));
      line_options.Append(ComputeScrollBarSuffix(options, line));
    }
    if (options.zoomed_out_tree != nullptr &&
        !options.zoomed_out_tree->children().empty()) {
      line_options.AppendString(
          DrawTree(line - initial_line(options).ToDelta(),
                   LineNumberDelta(options.screen_lines.size()),
                   *options.zoomed_out_tree),
          std::nullopt);
    }
  }
  return MakeNonNullShared<Line>(std::move(line_options));
}

std::list<MetadataLine> Prepare(const BufferMetadataOutputOptions& options,
                                Range range) {
  std::list<MetadataLine> output;
  const Line& contents = *options.buffer.contents().at(range.begin.line);
  auto target_buffer_value = contents.environment()->Lookup(
      Environment::Namespace(), L"buffer",
      vm::VMTypeMapper<std::shared_ptr<OpenBuffer>>::vmtype);
  const auto target_buffer =
      (target_buffer_value != nullptr &&
       target_buffer_value->user_value != nullptr)
          ? static_cast<OpenBuffer*>(target_buffer_value->user_value.get())
          : &options.buffer;

  auto info_char = L'•';
  auto info_char_modifier = LineModifier::DIM;

  if (target_buffer != &options.buffer) {
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
        if (metadata->size().IsZero()) return;
        ForEachColumn(*metadata, [](ColumnNumber, wchar_t c) {
          CHECK(c != L'\n') << "Metadata has invalid newline character.";
        });
        output.push_back(
            MetadataLine{L'>', LineModifier::GREEN,
                         MakeNonNullShared<const Line>(std::move(metadata)),
                         MetadataLine::Type::kLineContents});
      },
      [] {});

  std::list<LineMarks::Mark> marks;
  std::list<LineMarks::Mark> marks_expired;

  auto marks_range =
      options.buffer.GetLineMarks().equal_range(range.begin.line.line);
  while (marks_range.first != marks_range.second) {
    if (range.Contains(marks_range.first->second.target)) {
      (marks_range.first->second.IsExpired() ? marks_expired : marks)
          .push_back(marks_range.first->second);
    }
    ++marks_range.first;
  }

  for (const auto& mark : marks) {
    auto source = options.buffer.editor().buffers()->find(mark.source);
    output.push_back(MetadataLine{
        output.empty() ? L'!' : L' ',
        output.empty() ? LineModifier::RED : LineModifier::DIM,
        (source != options.buffer.editor().buffers()->end() &&
         mark.source_line < LineNumber(0) + source->second->contents().size())
            ? source->second->contents().at(mark.source_line)
            : MakeNonNullShared<const Line>(L"(dead mark)"),
        MetadataLine::Type::kMark});
  }

  // When an expired mark appears again, no need to show it redundantly (as
  // expired). We use `marks_strings` to detect this.
  std::set<std::wstring> marks_strings;
  for (const auto& mark : marks) {
    if (auto source = options.buffer.editor().buffers()->find(mark.source);
        source != options.buffer.editor().buffers()->end() &&
        mark.source_line < LineNumber(0) + source->second->contents().size()) {
      marks_strings.insert(
          source->second->contents().at(mark.source_line)->ToString());
    }
  }

  for (const auto& mark : marks_expired) {
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
// TODO(easy): Turn this into an iterative algorithm.
std::list<BoxWithPosition> RecursiveLayout(std::list<Box> boxes,
                                           LineNumberDelta screen_size,
                                           LineNumberDelta sum_sizes) {
  if (boxes.empty()) return {};
  Box back = std::move(boxes.back());
  boxes.pop_back();
  sum_sizes -= back.size;
  LineNumber position =
      max(LineNumber() + sum_sizes,
          min(back.reference, LineNumber() + screen_size - back.size));
  auto output =
      RecursiveLayout(std::move(boxes), position.ToDelta(), sum_sizes);
  output.push_back({.box = back, .position = position});
  return output;
}

std::list<BoxWithPosition> FindLayout(std::list<Box> boxes,
                                      LineNumberDelta screen_size) {
  boxes = Squash(std::move(boxes), screen_size);
  CHECK_LE(SumSizes(boxes), screen_size);
  return RecursiveLayout(std::move(boxes), screen_size, SumSizes(boxes));
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
      indents = max(indents, get(b.position + l).size());
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
