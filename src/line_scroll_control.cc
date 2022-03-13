#include "src/line_scroll_control.h"

#include <glog/logging.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <iostream>

#include "src/buffer.h"
#include "src/buffer_output_producer.h"
#include "src/buffer_variables.h"
#include "src/buffer_widget.h"
#include "src/char_buffer.h"
#include "src/line_output.h"
#include "src/tests/tests.h"
#include "src/vertical_split_output_producer.h"
#include "src/widget.h"
#include "src/wstring.h"

namespace afc::editor {
LineScrollControl::Reader::Reader(ConstructorAccessTag,
                                  std::shared_ptr<LineScrollControl> parent)
    : parent_(std::move(parent)) {
  CHECK(parent_ != nullptr);
}

LineScrollControl::LineScrollControl(ConstructorAccessTag, Options options)
    : options_(std::move(options)),
      cursors_([=]() {
        CHECK(options_.buffer != nullptr);
        std::map<LineNumber, std::set<ColumnNumber>> cursors;
        for (auto cursor : *options_.buffer->active_cursors()) {
          cursors[cursor.line].insert(cursor.column);
        }
        return cursors;
      }()),
      ranges_(ComputeRanges()) {}

std::unique_ptr<LineScrollControl::Reader> LineScrollControl::NewReader() {
  auto output = std::make_unique<LineScrollControl::Reader>(
      Reader::ConstructorAccessTag(), shared_from_this());
  readers_.push_back(output.get());
  return output;
}

std::optional<Range> LineScrollControl::Reader::GetRange() const {
  switch (state_) {
    case State::kDone:
      return std::nullopt;
    case State::kProcessing:
      return parent_->range();
  }
  LOG(FATAL) << "GetRange didn't handle all cases.";
  return std::nullopt;
}

bool LineScrollControl::Reader::HasActiveCursor() const {
  CHECK(state_ == State::kProcessing);
  return parent_->CurrentRangeContainsPosition(
      parent_->options_.buffer->position());
}

std::set<ColumnNumber> LineScrollControl::Reader::GetCurrentCursors() const {
  CHECK(state_ == State::kProcessing);
  Range range = parent_->range();

  LineNumber line = range.begin.line;
  auto it = parent_->cursors_.find(line);
  if (it == parent_->cursors_.end()) {
    return {};
  }
  std::set<ColumnNumber> output;
  for (auto& column : it->second) {
    if (parent_->CurrentRangeContainsPosition(LineColumn(line, column)))
      output.insert(column);
  }
  return output;
}

std::list<Range> LineScrollControl::ComputeRanges() const {
  std::list<Range> output;
  LineNumber line = options_.begin.line;
  while (LineNumberDelta(output.size()) < options_.lines_shown) {
    if (line > options_.buffer->EndLine()) {
      output.push_back(
          Range(LineColumn(line), std::numeric_limits<LineColumn>::max()));
      continue;
    }

    std::list<ColumnRange> line_breaks = BreakLineForOutput(
        *options_.buffer->LineAt(line), options_.columns_shown,
        options_.buffer->Read(buffer_variables::wrap_from_content)
            ? LineWrapStyle::kContentBased
            : LineWrapStyle::kBreakWords,
        options_.buffer->Read(buffer_variables::symbol_characters));
    while (LineNumberDelta(output.size()) < options_.lines_shown &&
           !line_breaks.empty()) {
      auto columns = line_breaks.front();
      output.push_back(
          Range::InLine(line, columns.begin, columns.end - columns.begin));
      line_breaks.pop_front();
    }
    ++line;
  }
  return output;
}

void LineScrollControl::SignalReaderDone() {
  if (++readers_done_ < readers_.size()) {
    VLOG(8) << "Readers done: " << readers_done_ << " out of "
            << readers_.size();
    return;
  }
  readers_done_ = 0;
  VLOG(6) << "Advancing, finished range: " << range();
  ranges_.pop_front();
  VLOG(7) << "Next range: " << range();

  for (auto& c : readers_) {
    c->state_ = Reader::State::kProcessing;
  }
}

Range LineScrollControl::range() const {
  return ranges_.empty()
             ? Range::InLine(options_.buffer->EndLine(),
                             options_.buffer->LineAt(options_.buffer->EndLine())
                                 ->EndColumn(),
                             std::numeric_limits<ColumnNumberDelta>::max())
             : ranges_.front();
}

Range LineScrollControl::next_range() const {
  return ranges_.size() < 2
             ? Range::InLine(options_.buffer->EndLine(),
                             options_.buffer->LineAt(options_.buffer->EndLine())
                                 ->EndColumn(),
                             std::numeric_limits<ColumnNumberDelta>::max())
             : *(std::next(ranges_.begin()));
}

bool LineScrollControl::CurrentRangeContainsPosition(
    LineColumn position) const {
  position = options_.buffer->AdjustLineColumn(position);
  return range().Contains(position) ||
         (position >= range().end && position < next_range().begin);
}

}  // namespace afc::editor
