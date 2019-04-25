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
                                     size_t line)
    : buffer_(buffer),
      cursors_([=]() {
        std::map<size_t, std::set<size_t>> cursors;
        for (auto cursor : *buffer_->active_cursors()) {
          cursors[cursor.line].insert(cursor.column);
        }
        return cursors;
      }()),
      line_(line) {}

std::unique_ptr<LineScrollControl::Reader> LineScrollControl::NewReader() {
  auto output = std::make_unique<LineScrollControl::Reader>(
      Reader::ConstructorAccessTag(), shared_from_this());
  readers_.push_back(output.get());
  return output;
}

bool LineScrollControl::Reader::HasActiveCursor() const {
  CHECK(state_ == State::kProcessing);
  return GetLine().value() == parent_->buffer_->position().line;
}

std::set<size_t> LineScrollControl::Reader::GetCurrentCursors() const {
  CHECK(state_ == State::kProcessing);
  auto line = GetLine().value();
  auto it = parent_->cursors_.find(line);
  if (it == parent_->cursors_.end()) {
    return {};
  }
  return it->second;
}

void LineScrollControl::SignalReaderDone() {
  if (++readers_done_ == readers_.size()) {
    readers_done_ = 0;
    line_++;
    for (auto& c : readers_) {
      c->state_ = Reader::State::kProcessing;
    }
  }
}

}  // namespace editor
}  // namespace afc
