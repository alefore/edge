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
#include "src/vertical_split_output_producer.h"
#include "src/widget.h"
#include "src/wstring.h"

namespace afc {
namespace editor {
LineScrollControl::Reader::Reader(ConstructorAccessTag,
                                  std::shared_ptr<LineScrollControl> parent)
    : parent_(std::move(parent)) {
  CHECK(parent_ != nullptr);
}

LineScrollControl::LineScrollControl(ConstructorAccessTag,
                                     std::shared_ptr<OpenBuffer> buffer,
                                     LineColumn view_start,
                                     size_t columns_shown)
    : buffer_(buffer),
      view_start_(view_start),
      columns_shown_(columns_shown),
      range_(view_start, view_start),
      cursors_([=]() {
        std::map<size_t, std::set<size_t>> cursors;
        for (auto cursor : *buffer_->active_cursors()) {
          cursors[cursor.line].insert(cursor.column);
        }
        return cursors;
      }()) {
  Advance();
}

std::unique_ptr<LineScrollControl::Reader> LineScrollControl::NewReader() {
  auto output = std::make_unique<LineScrollControl::Reader>(
      Reader::ConstructorAccessTag(), shared_from_this());
  readers_.push_back(output.get());
  return output;
}

bool LineScrollControl::Reader::HasActiveCursor() const {
  CHECK(state_ == State::kProcessing);
  return GetRange().value().Contains(parent_->buffer_->position());
}

std::set<size_t> LineScrollControl::Reader::GetCurrentCursors() const {
  CHECK(state_ == State::kProcessing);
  std::set<size_t> output;
  auto range = GetRange().value();
  auto it = parent_->cursors_.find(range.begin.line);
  if (it == parent_->cursors_.end()) {
    return output;
  }
  for (auto& c : it->second) {
    if (range.Contains(LineColumn(range.begin.line, c))) {
      output.insert(c - range.begin.column);
    }
  }
  return output;
}

void LineScrollControl::SignalReaderDone() {
  if (++readers_done_ == readers_.size()) {
    Advance();
  }
}

void LineScrollControl::Advance() {
  readers_done_ = 0;
  for (auto& c : readers_) {
    c->state_ = Reader::State::kProcessing;
  }

  range_.begin = range_.end;
  // TODO: This is wrong: it doesn't account for multi-width characters.
  // TODO: This is wrong: it doesn't take into account line filters.
  if (range_.begin.line >= buffer_->lines_size()) {
    range_.end = LineColumn(std::numeric_limits<size_t>::max());
    return;
  }
  range_.end =
      LineColumn(range_.begin.line, range_.begin.column + columns_shown_);
  if (range_.end.column < buffer_->LineAt(range_.end.line)->size() &&
      buffer_->Read(buffer_variables::wrap_long_lines())) {
    return;
  }
  range_.end.line++;
  range_.end.column = view_start_.column;
  if (range_.end.line >= buffer_->lines_size()) {
    range_.end = LineColumn(std::numeric_limits<size_t>::max());
  }
}

}  // namespace editor
}  // namespace afc
