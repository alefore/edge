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
#include "src/line_marks.h"
#include "src/output_producer.h"
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

BufferMetadataOutputProducer::BufferMetadataOutputProducer(
    std::shared_ptr<OpenBuffer> buffer,
    std::list<BufferContentsWindow::Line> screen_lines,
    LineNumberDelta lines_shown,
    std::shared_ptr<const ParseTree> zoomed_out_tree)
    : buffer_(std::move(buffer)),
      screen_lines_(std::move(screen_lines)),
      lines_shown_(lines_shown),
      root_(buffer_->parse_tree()),
      zoomed_out_tree_(std::move(zoomed_out_tree)) {}

OutputProducer::Generator BufferMetadataOutputProducer::Next() {
  if (screen_lines_.empty()) {
    return Generator::Empty();
  }

  Range range = screen_lines_.front().range;
  screen_lines_.pop_front();

  if (!initial_line_.has_value()) {
    initial_line_ = range.begin.line;
  }

  if (range.begin.line >= LineNumber(0) + buffer_->lines_size()) {
    return Generator::Empty();
  }

  Prepare(range);
  CHECK(!range_data_.empty());

  Generator output = std::move(range_data_.front());
  range_data_.pop_front();

  return output;
}

void BufferMetadataOutputProducer::Prepare(Range range) {
  std::list<Generator> previous_range_data;
  range_data_.swap(previous_range_data);

  auto contents = *buffer_->LineAt(range.begin.line);
  auto target_buffer_value = contents.environment()->Lookup(
      Environment::Namespace(), L"buffer",
      vm::VMTypeMapper<std::shared_ptr<OpenBuffer>>::vmtype);
  const auto target_buffer =
      (target_buffer_value != nullptr &&
       target_buffer_value->user_value != nullptr)
          ? static_cast<OpenBuffer*>(target_buffer_value->user_value.get())
          : buffer_.get();

  auto info_char = L'•';
  auto info_char_modifier = LineModifier::DIM;

  if (target_buffer != buffer_.get()) {
    if (buffers_shown_.insert(target_buffer).second) {
      PushGenerator(info_char, info_char_modifier,
                    Line(OpenBuffer::FlagsToString(target_buffer->Flags())));
    }
  } else if (contents.modified()) {
    info_char_modifier = LineModifier::GREEN;
    info_char = L'•';
  } else {
    info_char_modifier = LineModifier::DIM;
  }

  if (auto metadata = contents.metadata(); metadata != nullptr) {
    PushGenerator(L'>', LineModifier::GREEN, Line(metadata));
  }

  std::list<LineMarks::Mark> marks;
  std::list<LineMarks::Mark> marks_expired;

  auto marks_range =
      buffer_->GetLineMarks()->equal_range(range.begin.line.line);
  while (marks_range.first != marks_range.second) {
    if (range.Contains(marks_range.first->second.target)) {
      (marks_range.first->second.IsExpired() ? marks_expired : marks)
          .push_back(marks_range.first->second);
    }
    ++marks_range.first;
  }

  for (const auto& mark : marks) {
    auto source = buffer_->editor()->buffers()->find(mark.source);
    PushGenerator(
        range_data_.empty() ? L'!' : L' ',
        range_data_.empty() ? LineModifier::RED : LineModifier::DIM,
        (source != buffer_->editor()->buffers()->end() &&
         mark.source_line < LineNumber(0) + source->second->contents()->size())
            ? *source->second->contents()->at(mark.source_line)
            : Line(L"(dead mark)"));
  }

  // When an expired mark appears again, no need to show it redundantly (as
  // expired). We use `marks_strings` to detect this.
  std::set<std::wstring> marks_strings;
  for (const auto& mark : marks) {
    if (auto source = buffer_->editor()->buffers()->find(mark.source);
        source != buffer_->editor()->buffers()->end() &&
        mark.source_line < LineNumber(0) + source->second->contents()->size()) {
      marks_strings.insert(
          source->second->contents()->at(mark.source_line)->ToString());
    }
  }

  for (const auto& mark : marks_expired) {
    if (auto contents = mark.source_line_content->ToString();
        marks_strings.find(contents) == marks_strings.end()) {
      PushGenerator('!', LineModifier::RED, Line(L"👻 " + contents));
    }
  }

  if (range_data_.empty()) {
    if (previous_range_data.empty()) {
      PushGenerator(info_char, info_char_modifier,
                    GetDefaultInformation(range.begin.line));
    } else {
      range_data_ = std::move(previous_range_data);  // Carry over.
    }
  }
  CHECK(!range_data_.empty());
}

Line BufferMetadataOutputProducer::GetDefaultInformation(LineNumber line) {
  Line::Options options;
  auto parse_tree = buffer_->simplified_parse_tree();
  if (parse_tree != nullptr) {
    options.AppendString(DrawTree(line, buffer_->lines_size(), *parse_tree),
                         std::nullopt);
  }
  if (buffer_->Read(buffer_variables::scrollbar) &&
      buffer_->lines_size() > lines_shown_) {
    CHECK_GE(line, initial_line_.value());
    options.Append(ComputeCursorsSuffix(line));
    options.Append(ComputeTagsSuffix(line));
    options.Append(ComputeScrollBarSuffix(line));
  }
  if (zoomed_out_tree_ != nullptr && !zoomed_out_tree_->children().empty()) {
    options.AppendString(DrawTree(line - initial_line_.value().ToDelta(),
                                  lines_shown_, *zoomed_out_tree_),
                         std::nullopt);
  }
  return Line(std::move(options));
}

void BufferMetadataOutputProducer::PushGenerator(wchar_t info_char,
                                                 LineModifier modifier,
                                                 Line suffix) {
  range_data_.push_back(Generator::New(CaptureAndHash(
      [](wchar_t info_char, LineModifier modifier, Line suffix) {
        Line::Options options;
        options.AppendCharacter(info_char, {modifier});
        options.Append(suffix);
        return LineWithCursor{std::make_shared<Line>(options), std::nullopt};
      },
      info_char, modifier, suffix)));
}

// Assume that the screen is currently showing the screen_position lines out of
// a buffer of size total_size. Map current_line to its associated range of
// lines (for the purposes of the scroll bar). The columns are entirely ignored
// by this function.
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

Line BufferMetadataOutputProducer::ComputeTagsSuffix(LineNumber line) {
  CHECK(initial_line_.has_value());
  CHECK_GE(line, initial_line_.value());
  const std::multimap<size_t, LineMarks::Mark>* marks = buffer_->GetLineMarks();
  if (marks->empty()) return Line(L"");
  auto range = MapScreenLineToContentsRange(
      Range(LineColumn(LineNumber(initial_line_.value())),
            LineColumn(LineNumber(initial_line_.value() + lines_shown_))),
      line, buffer_->lines_size());

  if (marks->lower_bound(range.begin.line.line) ==
      marks->lower_bound(range.end.line.line))
    return Line(L" ");
  Line::Options options;
  options.AppendString(L"!", LineModifierSet({LineModifier::RED}));
  return Line(options);
}

Line BufferMetadataOutputProducer::ComputeCursorsSuffix(LineNumber line) {
  auto cursors = buffer_->active_cursors();
  if (cursors->size() <= 1) {
    return Line(L"");
  }
  CHECK(initial_line_.has_value());
  CHECK_GE(line, initial_line_.value());
  auto range = MapScreenLineToContentsRange(
      Range(LineColumn(LineNumber(initial_line_.value())),
            LineColumn(LineNumber(initial_line_.value() + lines_shown_))),
      line, buffer_->lines_size());
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
  Line::Options options;
  options.AppendString(output_str, modifiers);
  return Line(std::move(options));
}

Line BufferMetadataOutputProducer::ComputeScrollBarSuffix(LineNumber line) {
  auto lines_size = buffer_->lines_size();
  // Each line is split into two units (upper and bottom halves). All units in
  // this function are halves (of a line).
  DCHECK_GE(line, initial_line_.value());
  DCHECK_LE(line - initial_line_.value(), lines_shown_)
      << "Line is " << line << " and view_start is " << initial_line_.value()
      << ", which exceeds lines_shown_ of " << lines_shown_;
  DCHECK_LT(initial_line_.value(), LineNumber(0) + lines_size);
  size_t halves_to_show = lines_shown_.line_delta * 2;

  // Number of halves the bar should take.
  size_t bar_size =
      max(size_t(1),
          size_t(std::round(halves_to_show *
                            static_cast<double>(lines_shown_.line_delta) /
                            lines_size.line_delta)));

  // Bar will be shown in lines in interval [bar, end] (units are halves).
  size_t start = std::round(halves_to_show *
                            static_cast<double>(initial_line_.value().line) /
                            lines_size.line_delta);
  size_t end = start + bar_size;

  LineModifierSet modifiers =
      MapScreenLineToContentsRange(
          Range(LineColumn(LineNumber(initial_line_.value())),
                LineColumn(LineNumber(initial_line_.value() + lines_shown_))),
          line, buffer_->lines_size())
              .Contains(buffer_->position())
          ? LineModifierSet({LineModifier::BLUE})
          : LineModifierSet({LineModifier::CYAN});

  Line::Options options;
  size_t current = 2 * (line - initial_line_.value()).line_delta;
  if (current < start - (start % 2) || current >= end) {
    options.AppendString(L" ", modifiers);
  } else if (start == current + 1) {
    options.AppendString(L"▄", modifiers);
  } else if (current + 1 == end) {
    options.AppendString(L"▀", modifiers);
  } else {
    options.AppendString(L"█", modifiers);
  }
  return Line(std::move(options));
}
}  // namespace editor
}  // namespace afc
