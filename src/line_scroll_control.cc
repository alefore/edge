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
LineScrollControl::LineScrollControl(std::shared_ptr<OpenBuffer> buffer,
                                     LineColumn view_start,
                                     size_t columns_shown, size_t customers)
    : buffer_(buffer),
      view_start_(view_start),
      columns_shown_(columns_shown),
      customers_(customers),
      range_(view_start, view_start),
      cursors_([=]() {
        std::map<size_t, std::set<size_t>> cursors;
        for (auto cursor : *buffer_->active_cursors()) {
          cursors[cursor.line].insert(cursor.column);
        }
        return cursors;
      }()),
      customers_done_(customers_) {
  Advance();
}

bool LineScrollControl::HasActiveCursor() const {
  return GetRange().Contains(buffer_->position());
}

std::set<size_t> LineScrollControl::GetCurrentCursors() const {
  std::set<size_t> output;
  auto it = cursors_.find(range_.begin.line);
  if (it == cursors_.end()) {
    return output;
  }
  auto range = GetRange();
  for (auto& c : it->second) {
    if (range.Contains(LineColumn(range_.begin.line, c))) {
      output.insert(c - range_.begin.column);
    }
  }
  return output;
}

void LineScrollControl::Advance() {
  if (++customers_done_ < customers_) {
    return;
  }
  customers_done_ = 0;
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
