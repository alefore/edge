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
      line_(options_.begin.line),
      line_breaks_(ComputeBreaks()) {}

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
  return parent_->range().Contains(parent_->options_.buffer->position());
}

std::set<ColumnNumber> LineScrollControl::Reader::GetCurrentCursors() const {
  CHECK(state_ == State::kProcessing);
  LineNumber line = parent_->range().begin.line;
  auto it = parent_->cursors_.find(line);
  if (it == parent_->cursors_.end()) {
    return {};
  }
  std::set<ColumnNumber> output;
  for (auto& column : it->second) {
    if (parent_->range().Contains(LineColumn(line, column))) {
      output.insert(column);
    }
  }
  return output;
}

std::list<ColumnRange> LineScrollControl::ComputeBreaks() const {
  if (line_ > options_.buffer->EndLine())
    return std::list<ColumnRange>(
        {{ColumnNumber(), std::numeric_limits<ColumnNumber>::max()}});
  return BreakLineForOutput(
      *options_.buffer->LineAt(line_), options_.columns_shown,
      options_.buffer->Read(buffer_variables::wrap_from_content)
          ? LineWrapStyle::kContentBased
          : LineWrapStyle::kBreakWords,
      options_.buffer->Read(buffer_variables::symbol_characters));
}

void LineScrollControl::SignalReaderDone() {
  if (++readers_done_ < readers_.size()) {
    VLOG(8) << "Readers done: " << readers_done_ << " out of "
            << readers_.size();
    return;
  }
  readers_done_ = 0;
  VLOG(6) << "Advancing, finished range: " << range();
  line_breaks_.pop_front();
  if (line_breaks_.empty()) {
    if (line_ <= options_.buffer->EndLine()) ++line_;
    line_breaks_ = ComputeBreaks();
    CHECK(!line_breaks_.empty());
  }
  VLOG(7) << "Next range: " << range();

  for (auto& c : readers_) {
    c->state_ = Reader::State::kProcessing;
  }
}

Range LineScrollControl::range() const {
  CHECK(!line_breaks_.empty());
  LineColumn begin(line_, line_breaks_.front().begin);
  if (line_ > options_.buffer->EndLine()) {
    return Range(begin, LineColumn::Max());
  }

  return Range(begin, LineColumn(line_, line_breaks_.front().end));
}
}  // namespace afc::editor
