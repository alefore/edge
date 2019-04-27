#include "src/buffer_metadata_output_producer.h"

#include <glog/logging.h>

#include <algorithm>
#include <cctype>
#include <iostream>

#include "src/buffer.h"
#include "src/buffer_variables.h"
#include "src/cursors_highlighter.h"
#include "src/delegating_output_receiver.h"
#include "src/delegating_output_receiver_with_internal_modifiers.h"
#include "src/dirname.h"
#include "src/line_marks.h"
#include "src/output_receiver.h"
#include "src/output_receiver_optimizer.h"
#include "src/parse_tree.h"
#include "src/screen_output_receiver.h"
#include "src/terminal.h"

namespace afc {
namespace editor {
namespace {
wchar_t ComputeScrollBarCharacter(size_t line, size_t lines_size,
                                  size_t view_start, size_t lines_shown_) {
  // Each line is split into two units (upper and bottom halves). All units in
  // this function are halves (of a line).
  DCHECK_GE(line, view_start);
  DCHECK_LT(line - view_start, lines_shown_)
      << "Line is " << line << " and view_start is " << view_start
      << ", which exceeds lines_shown_ of " << lines_shown_;
  DCHECK_LT(view_start, lines_size);
  size_t halves_to_show = lines_shown_ * 2;

  // Number of halves the bar should take.
  size_t bar_size =
      max(size_t(1),
          size_t(std::round(halves_to_show * static_cast<double>(lines_shown_) /
                            lines_size)));

  // Bar will be shown in lines in interval [bar, end] (units are halves).
  size_t start =
      std::round(halves_to_show * static_cast<double>(view_start) / lines_size);
  size_t end = start + bar_size;

  size_t current = 2 * (line - view_start);
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

wstring DrawTree(size_t line, size_t lines_size, const ParseTree& root) {
  // Route along the tree where each child ends after previous line.
  vector<const ParseTree*> route_begin;
  if (line > 0) {
    route_begin = MapRoute(
        root,
        FindRouteToPosition(
            root, LineColumn(line - 1, std::numeric_limits<size_t>::max())));
    CHECK(!route_begin.empty() && *route_begin.begin() == &root);
    route_begin.erase(route_begin.begin());
  }

  // Route along the tree where each child ends after current line.
  vector<const ParseTree*> route_end;
  if (line < lines_size - 1) {
    route_end = MapRoute(
        root, FindRouteToPosition(
                  root, LineColumn(line, std::numeric_limits<size_t>::max())));
    CHECK(!route_end.empty() && *route_end.begin() == &root);
    route_end.erase(route_end.begin());
  }

  wstring output(root.depth, L' ');
  size_t index_begin = 0;
  size_t index_end = 0;
  while (index_begin < route_begin.size() || index_end < route_end.size()) {
    if (index_begin == route_begin.size()) {
      Draw(route_end[index_end]->depth, L'─', L'╮', L'┬', &output);
      index_end++;
      continue;
    }
    if (index_end == route_end.size()) {
      Draw(route_begin[index_begin]->depth, L'─', L'╯', L'┴', &output);
      index_begin++;
      continue;
    }

    if (route_begin[index_begin]->depth > route_end[index_end]->depth) {
      Draw(route_begin[index_begin]->depth, L'─', L'╯', L'┴', &output);
      index_begin++;
      continue;
    }

    if (route_end[index_end]->depth > route_begin[index_begin]->depth) {
      Draw(route_end[index_end]->depth, L'─', L'╮', L'┬', &output);
      index_end++;
      continue;
    }

    if (route_begin[index_begin] == route_end[index_end]) {
      output[route_begin[index_begin]->depth] = L'│';
      index_begin++;
      index_end++;
      continue;
    }

    Draw(route_end[index_end]->depth, L'─', L'┤', L'┼', &output);
    index_begin++;
    index_end++;
  }
  return output;
}
}  // namespace

BufferMetadataOutputProducer::BufferMetadataOutputProducer(
    std::shared_ptr<OpenBuffer> buffer,
    std::unique_ptr<LineScrollControl::Reader> line_scroll_control_reader,
    size_t lines_shown, size_t initial_line,
    std::shared_ptr<const ParseTree> zoomed_out_tree)
    : buffer_(std::move(buffer)),
      line_scroll_control_reader_(std::move(line_scroll_control_reader)),
      lines_shown_(lines_shown),
      initial_line_(initial_line),
      root_(buffer_->parse_tree()),
      zoomed_out_tree_(std::move(zoomed_out_tree)) {}

void AddString(wchar_t info_char, LineModifier modifier, wstring str,
               OutputReceiver* receiver) {
  receiver->AddModifier(modifier);
  receiver->AddCharacter(info_char);
  receiver->AddModifier(LineModifier::RESET);
  receiver->AddString(str);
}

void BufferMetadataOutputProducer::WriteLine(Options options) {
  auto line = line_scroll_control_reader_->GetLine();
  if (!line.has_value()) {
    return;
  }

  if (line.value() >= buffer_->lines_size()) {
    options.receiver->AddString(L"\n");
    line_scroll_control_reader_->LineDone();
    return;
  }

  if (line_data_ == std::nullopt) {
    Prepare(line.value());
  }

  if (line_data_->additional_information_.has_value()) {
    AddString(line_data_->info_char_, line_data_->info_char_modifier_,
              line_data_->additional_information_.value(),
              options.receiver.get());
    line_data_->additional_information_ = std::nullopt;
  } else if (!line_data_->marks_.empty()) {
    const auto& m = line_data_->marks_.front();
    auto source = buffer_->editor()->buffers()->find(m.source);
    if (source != buffer_->editor()->buffers()->end() &&
        source->second->contents()->size() > m.source_line) {
      AddString('!', LineModifier::RED,
                source->second->contents()->at(m.source_line)->ToString(),
                options.receiver.get());
    } else {
      AddString('!', LineModifier::RED, L"(dead mark)", options.receiver.get());
    }
    line_data_->marks_.pop_front();
  } else if (!line_data_->marks_expired_.empty()) {
    AddString(
        '!', LineModifier::RED,
        L"👻 " +
            line_data_->marks_expired_.front().source_line_content->ToString(),
        options.receiver.get());
    line_data_->marks_expired_.pop_front();
  } else {
    AddString(line_data_->info_char_, line_data_->info_char_modifier_,
              GetDefaultInformation(line.value()), options.receiver.get());
  }

  if (line_data_->additional_information_ == std::nullopt &&
      line_data_->marks_.empty() && line_data_->marks_expired_.empty()) {
    line_data_ = std::nullopt;
    line_scroll_control_reader_->LineDone();
  }
}

void BufferMetadataOutputProducer::Prepare(size_t line) {
  CHECK(line_data_ == std::nullopt);
  line_data_ = LineData();

  auto contents = *buffer_->LineAt(line);
  auto target_buffer_value = contents.environment()->Lookup(
      L"buffer", vm::VMTypeMapper<std::shared_ptr<OpenBuffer>>::vmtype);
  const auto target_buffer =
      (target_buffer_value != nullptr &&
       target_buffer_value->user_value != nullptr)
          ? static_cast<OpenBuffer*>(target_buffer_value->user_value.get())
          : buffer_.get();

  auto marks = buffer_->GetLineMarks()->equal_range(line);
  if (marks.first != marks.second) {
    while (std::next(marks.first) != marks.second) {
      (marks.first->second.IsExpired() ? line_data_->marks_expired_
                                       : line_data_->marks_)
          .push_back(marks.first->second);
      ++marks.first;
    }
  }

  line_data_->info_char_ = L'•';
  line_data_->info_char_modifier_ = LineModifier::DIM;

  if (target_buffer != buffer_.get()) {
    if (buffers_shown_.insert(target_buffer).second) {
      line_data_->additional_information_ = target_buffer->FlagsString();
    }
  } else if (marks.first != marks.second) {
  } else if (contents.modified()) {
    line_data_->info_char_modifier_ = LineModifier::GREEN;
    line_data_->info_char_ = L'•';
  } else {
    line_data_->info_char_modifier_ = LineModifier::DIM;
  }
}

wstring BufferMetadataOutputProducer::GetDefaultInformation(size_t line) {
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
  if (zoomed_out_tree_ != nullptr && !zoomed_out_tree_->children.empty()) {
    output += DrawTree(line - initial_line_, lines_shown_, *zoomed_out_tree_);
  }
  return output;
}

}  // namespace editor
}  // namespace afc
