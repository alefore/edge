#include "src/buffer_metadata_output_producer.h"

#include <glog/logging.h>

#include <algorithm>
#include <cctype>
#include <iostream>

#include "src/buffer.h"
#include "src/buffer_variables.h"
#include "src/char_buffer.h"
#include "src/dirname.h"
#include "src/line_marks.h"
#include "src/output_producer.h"
#include "src/parse_tree.h"
#include "src/terminal.h"

namespace afc {
namespace editor {
namespace {
// TODO: Make a private method of BufferMetadataOutputProducer?
wchar_t ComputeScrollBarCharacter(LineNumber line, LineNumberDelta lines_size,
                                  LineNumber view_start,
                                  LineNumberDelta lines_shown_) {
  // Each line is split into two units (upper and bottom halves). All units in
  // this function are halves (of a line).
  DCHECK_GE(line, view_start);
  DCHECK_LT(line - view_start, lines_shown_)
      << "Line is " << line << " and view_start is " << view_start
      << ", which exceeds lines_shown_ of " << lines_shown_;
  DCHECK_LT(view_start, LineNumber(0) + lines_size);
  size_t halves_to_show = lines_shown_.line_delta * 2;

  // Number of halves the bar should take.
  size_t bar_size =
      max(size_t(1),
          size_t(std::round(halves_to_show *
                            static_cast<double>(lines_shown_.line_delta) /
                            lines_size.line_delta)));

  // Bar will be shown in lines in interval [bar, end] (units are halves).
  size_t start =
      std::round(halves_to_show * static_cast<double>(view_start.line) /
                 lines_size.line_delta);
  size_t end = start + bar_size;

  size_t current = 2 * (line - view_start).line_delta;
  if (current < start - (start % 2) || current >= end) {
    return L' ';
  } else if (start == current + 1) {
    return L'▄';
  } else if (current + 1 == end) {
    return L'▀';
  } else {
    return L'█';
  }
}

void Draw(size_t pos, wchar_t padding_char, wchar_t final_char,
          wchar_t connect_final_char, wstring* output) {
  CHECK_LT(pos, output->size());
  for (size_t i = 0; i < pos; i++) {
    output->at(i) = padding_char;
  }
  output->at(pos) = (pos + 1 == output->size() || output->at(pos + 1) == L' ' ||
                     output->at(pos + 1) == L'│')
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
      Draw(route_end[index_end]->depth(), L'─', L'╮', L'┬', &output);
      index_end++;
      continue;
    }
    if (index_end == route_end.size()) {
      Draw(route_begin[index_begin]->depth(), L'─', L'╯', L'┴', &output);
      index_begin++;
      continue;
    }

    if (route_begin[index_begin]->depth() > route_end[index_end]->depth()) {
      Draw(route_begin[index_begin]->depth(), L'─', L'╯', L'┴', &output);
      index_begin++;
      continue;
    }

    if (route_end[index_end]->depth() > route_begin[index_begin]->depth()) {
      Draw(route_end[index_end]->depth(), L'─', L'╮', L'┬', &output);
      index_end++;
      continue;
    }

    if (route_begin[index_begin] == route_end[index_end]) {
      output[route_begin[index_begin]->depth()] = L'│';
      index_begin++;
      index_end++;
      continue;
    }

    Draw(route_end[index_end]->depth(), L'─', L'┤', L'┼', &output);
    index_begin++;
    index_end++;
  }
  return output;
}
}  // namespace

BufferMetadataOutputProducer::BufferMetadataOutputProducer(
    std::shared_ptr<OpenBuffer> buffer,
    std::unique_ptr<LineScrollControl::Reader> line_scroll_control_reader,
    LineNumberDelta lines_shown, LineNumber initial_line,
    std::shared_ptr<const ParseTree> zoomed_out_tree)
    : buffer_(std::move(buffer)),
      line_scroll_control_reader_(std::move(line_scroll_control_reader)),
      lines_shown_(lines_shown),
      initial_line_(initial_line),
      root_(buffer_->parse_tree()),
      zoomed_out_tree_(std::move(zoomed_out_tree)) {}

OutputProducer::Generator BufferMetadataOutputProducer::Next() {
  auto range = line_scroll_control_reader_->GetRange();
  if (!range.has_value()) {
    return Generator::Empty();
  }

  if (range.value().begin.line >= LineNumber(0) + buffer_->lines_size()) {
    line_scroll_control_reader_->RangeDone();
    return Generator::Empty();
  }

  if (range_data_.empty()) {
    Prepare(range.value());
    CHECK(!range_data_.empty());
  }

  Generator output = std::move(range_data_.front());
  range_data_.pop_front();

  if (range_data_.empty()) {
    line_scroll_control_reader_->RangeDone();
  }

  return output;
}

void BufferMetadataOutputProducer::Prepare(Range range) {
  CHECK(range_data_.empty());

  auto contents = *buffer_->LineAt(range.begin.line);
  auto target_buffer_value = contents.environment()->Lookup(
      L"buffer", vm::VMTypeMapper<std::shared_ptr<OpenBuffer>>::vmtype);
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
                    OpenBuffer::FlagsToString(target_buffer->Flags()));
    }
  } else if (contents.modified()) {
    info_char_modifier = LineModifier::GREEN;
    info_char = L'•';
  } else {
    info_char_modifier = LineModifier::DIM;
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
        '!', LineModifier::RED,

        (source != buffer_->editor()->buffers()->end() &&
         mark.source_line < LineNumber(0) + source->second->contents()->size())
            ? source->second->contents()->at(mark.source_line)->ToString()
            : L"(dead mark)");
  }

  for (const auto& mark : marks_expired) {
    PushGenerator('!', LineModifier::RED,
                  L"👻 " + mark.source_line_content->ToString());
  }

  if (range_data_.empty()) {
    PushGenerator(info_char, info_char_modifier,
                  GetDefaultInformation(range.begin.line));
  }
}

wstring BufferMetadataOutputProducer::GetDefaultInformation(LineNumber line) {
  wstring output;
  auto parse_tree = buffer_->simplified_parse_tree();
  if (parse_tree != nullptr) {
    output += DrawTree(line, buffer_->lines_size(), *parse_tree);
  }
  if (buffer_->Read(buffer_variables::scrollbar) &&
      buffer_->lines_size() > lines_shown_) {
    CHECK_GE(line, initial_line_);
    output += ComputeScrollBarCharacter(line, buffer_->lines_size(),
                                        initial_line_, lines_shown_);
  }
  if (zoomed_out_tree_ != nullptr && !zoomed_out_tree_->children().empty()) {
    output += DrawTree(line - initial_line_.ToDelta(), lines_shown_,
                       *zoomed_out_tree_);
  }
  return output;
}

template <class T>
inline void hash_combine(std::size_t& seed, const T& v) {
  std::hash<T> hasher;
  seed ^= hasher(v) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
}

void BufferMetadataOutputProducer::PushGenerator(wchar_t info_char,
                                                 LineModifier modifier,
                                                 wstring str) {
  size_t hash = 0;
  hash_combine(hash, info_char);
  hash_combine(hash, static_cast<int>(modifier));
  hash_combine(hash, str);

  range_data_.push_back(Generator{
      hash, [info_char, modifier, str]() {
        Line::Options options;
        options.AppendCharacter(info_char, {modifier});
        options.AppendString(NewLazyString(str));
        return LineWithCursor{std::make_shared<Line>(options), std::nullopt};
      }});
}

}  // namespace editor
}  // namespace afc
