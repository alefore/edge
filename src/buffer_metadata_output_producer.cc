#include "src/buffer_metadata_output_producer.h"

#include <glog/logging.h>

#include <algorithm>
#include <cctype>
#include <iostream>

#include "src/buffer.h"
#include "src/buffer_variables.h"
#include "src/char_buffer.h"
#include "src/dirname.h"
#include "src/hash.h"
#include "src/lazy_string_functional.h"
#include "src/line_marks.h"
#include "src/line_with_cursor.h"
#include "src/parse_tree.h"
#include "src/terminal.h"

namespace afc {
namespace editor {
namespace {
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
}  // namespace

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

namespace {
ColumnNumberDelta width(MetadataLine& line, bool has_previous, bool has_next) {
  return ColumnNumberDelta(1) +
         (has_previous || has_next ? ColumnNumberDelta(1)
                                   : ColumnNumberDelta(0)) +
         line.suffix.contents()->size();
}

LineWithCursor::Generator NewGenerator(MetadataLine line, bool has_previous,
                                       bool has_next, bool is_start) {
  return LineWithCursor::Generator::New(CaptureAndHash(
      [](wchar_t info_char, LineModifier modifier, Line suffix,
         bool has_previous, bool has_next, bool is_start) {
        Line::Options options;
        options.AppendCharacter(info_char, {modifier});
        if (is_start) {
          if (has_previous && has_next) {
            options.AppendCharacter(L'╈', {});
          } else if (has_previous) {
            // Pass.
          } else if (has_next) {
            options.AppendCharacter(L'┳', {});
          }
        } else {
          if (has_previous && has_next) {
            options.AppendCharacter(L'┃', {});
          } else if (has_previous) {
            options.AppendCharacter(L'┗', {});
          } else if (has_next) {
            options.AppendCharacter(L'┳', {});
          }
        }
        options.Append(suffix);
        return LineWithCursor{Line(options)};
      },
      line.info_char, line.modifier, std::move(line.suffix), has_previous,
      has_next, is_start));
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
  // Each line is split into two units (upper and bottom halves). All units in
  // this function are halves (of a line).
  DCHECK_GE(line, initial_line(options));
  DCHECK_LE(line - initial_line(options), lines_shown)
      << "Line is " << line << " and view_start is " << initial_line(options)
      << ", which exceeds lines_shown_ of " << lines_shown;
  DCHECK_LT(initial_line(options), LineNumber(0) + lines_size);
  size_t halves_to_show = lines_shown.line_delta * 2;

  // Number of halves the bar should take.
  size_t bar_size = max(
      size_t(1), size_t(std::round(halves_to_show *
                                   static_cast<double>(lines_shown.line_delta) /
                                   lines_size.line_delta)));

  // Bar will be shown in lines in interval [bar, end] (units are halves).
  size_t start = std::round(halves_to_show *
                            static_cast<double>(initial_line(options).line) /
                            lines_size.line_delta);
  size_t end = start + bar_size;

  LineModifierSet modifiers =
      MapScreenLineToContentsRange(
          Range(LineColumn(LineNumber(initial_line(options))),
                LineColumn(LineNumber(initial_line(options) + lines_shown))),
          line, options.buffer.lines_size())
              .Contains(options.buffer.position())
          ? LineModifierSet({LineModifier::BLUE})
          : LineModifierSet({LineModifier::CYAN});

  Line::Options line_options;
  size_t current = 2 * (line - initial_line(options)).line_delta;
  if (current < start - (start % 2) || current >= end) {
    line_options.AppendString(L" ", modifiers);
  } else if (start == current + 1) {
    line_options.AppendString(L"▄", modifiers);
  } else if (current + 1 == end) {
    line_options.AppendString(L"▀", modifiers);
  } else {
    line_options.AppendString(L"█", modifiers);
  }
  return Line(std::move(line_options));
}

Line GetDefaultInformation(const BufferMetadataOutputOptions& options,
                           LineNumber line) {
  Line::Options line_options;
  auto parse_tree = options.buffer.simplified_parse_tree();
  if (parse_tree != nullptr) {
    line_options.AppendString(
        DrawTree(line, options.buffer.lines_size(), *parse_tree), std::nullopt);
  }

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
  return Line(std::move(line_options));
}

std::list<MetadataLine> Prepare(const BufferMetadataOutputOptions& options,
                                Range range, bool has_previous) {
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
                     Line(OpenBuffer::FlagsToString(target_buffer->Flags())),
                     MetadataLine::Type::kFlags});
  } else if (contents.modified()) {
    info_char_modifier = LineModifier::GREEN;
    info_char = L'•';
  } else {
    info_char_modifier = LineModifier::DIM;
  }

  if (auto metadata = contents.metadata();
      metadata != nullptr && !metadata->size().IsZero()) {
    ForEachColumn(*metadata, [](ColumnNumber, wchar_t c) {
      CHECK(c != L'\n') << "Metadata has invalid newline character.";
    });
    output.push_back(MetadataLine{L'>', LineModifier::GREEN, Line(metadata),
                                  MetadataLine::Type::kLineContents});
  }

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
            ? *source->second->contents().at(mark.source_line)
            : Line(L"(dead mark)"),
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
                                    Line(L"👻 " + contents),
                                    MetadataLine::Type::kMark});
    }
  }

  if (output.empty() && !has_previous) {
    output.push_back(
        MetadataLine{info_char, info_char_modifier,
                     GetDefaultInformation(options, range.begin.line),
                     MetadataLine::Type::kDefault});
  }
  CHECK(!output.empty() || has_previous);
  return output;
}

LineWithCursor::Generator::Vector BufferMetadataOutput(
    BufferMetadataOutputOptions options) {
  if (options.screen_lines.empty()) return {};
  LineWithCursor::Generator::Vector output;
  std::list<MetadataLine> range_data;
  for (LineNumberDelta i; i < LineNumberDelta(options.screen_lines.size());
       ++i) {
    Range range = options.screen_lines[i.line_delta].range;
    if (range.begin.line >= LineNumber(0) + options.buffer.lines_size()) {
      continue;
    }

    bool has_previous = !range_data.empty();
    bool is_start = false;
    if (std::list<MetadataLine> new_range =
            Prepare(options, range, !range_data.empty());
        !new_range.empty()) {
      range_data.swap(new_range);
      is_start = true;
    }
    CHECK(!range_data.empty());

    bool has_next = range_data.size() > 1;
    output.width = std::max(output.width,
                            width(range_data.front(), has_previous, has_next));
    output.lines.push_back(NewGenerator(std::move(range_data.front()),
                                        has_previous, has_next, is_start));
    range_data.pop_front();
  }
  return output;
}

}  // namespace editor
}  // namespace afc
