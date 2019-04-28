#include "src/buffer_tree_horizontal.h"

#include <glog/logging.h>

#include <algorithm>
#include <cctype>
#include <ctgmath>
#include <iostream>
#include <numeric>

#include "src/buffer.h"
#include "src/buffer_output_producer.h"
#include "src/buffer_variables.h"
#include "src/buffer_widget.h"
#include "src/buffers_list.h"
#include "src/frame_output_producer.h"
#include "src/horizontal_split_output_producer.h"
#include "src/widget.h"
#include "src/wstring.h"

namespace afc {
namespace editor {

BufferTreeHorizontal::~BufferTreeHorizontal() {}

/* static */ std::unique_ptr<BufferTreeHorizontal> BufferTreeHorizontal::New(
    std::vector<std::unique_ptr<Widget>> children, size_t active) {
  return std::make_unique<BufferTreeHorizontal>(ConstructorAccessTag(),
                                                std::move(children), active);
}

/* static */ std::unique_ptr<BufferTreeHorizontal> BufferTreeHorizontal::New(
    std::unique_ptr<Widget> child) {
  std::vector<std::unique_ptr<Widget>> children;
  children.push_back(std::move(child));
  return BufferTreeHorizontal::New(std::move(children), 0);
}

BufferTreeHorizontal::BufferTreeHorizontal(
    ConstructorAccessTag, std::vector<std::unique_ptr<Widget>> children,
    size_t active)
    : children_(std::move(children)), active_(active) {}

wstring BufferTreeHorizontal::Name() const { return L""; }

wstring BufferTreeHorizontal::ToString() const {
  wstring output = L"[buffer tree horizontal, children: " +
                   std::to_wstring(children_.size()) + L", active: " +
                   std::to_wstring(active_) + L"]";
  return output;
}

BufferWidget* BufferTreeHorizontal::GetActiveLeaf() {
  CHECK(!children_.empty());
  CHECK_LT(active_, children_.size());
  CHECK(children_[active_] != nullptr);
  return children_[active_]->GetActiveLeaf();
}

std::unique_ptr<OutputProducer> BufferTreeHorizontal::CreateOutputProducer() {
  std::vector<HorizontalSplitOutputProducer::Row> rows;
  CHECK_EQ(children_.size(), lines_per_child_.size());

  bool show_frames =
      std::count_if(lines_per_child_.begin(), lines_per_child_.end(),
                    [](size_t i) { return i > 0; }) > 1;

  for (size_t index = 0; index < children_.size(); index++) {
    auto child_producer = children_[index]->CreateOutputProducer();
    CHECK(child_producer != nullptr);
    std::shared_ptr<const OpenBuffer> buffer =
        children_[index]->GetActiveLeaf()->Lock();
    if (show_frames) {
      VLOG(5) << "Producing row with frame.";
      std::vector<HorizontalSplitOutputProducer::Row> nested_rows;
      FrameOutputProducer::FrameOptions frame_options;
      frame_options.title = children_[index]->Name();
      frame_options.position_in_parent = index;
      if (index == active_) {
        frame_options.active_state =
            FrameOutputProducer::FrameOptions::ActiveState::kActive;
      }
      if (buffer != nullptr) {
        frame_options.extra_information = buffer->FlagsString();
      }
      nested_rows.push_back(
          {std::make_unique<FrameOutputProducer>(std::move(frame_options)), 1});
      nested_rows.push_back(
          {std::move(child_producer), lines_per_child_[index] - 1});
      child_producer = std::make_unique<HorizontalSplitOutputProducer>(
          std::move(nested_rows), 1);
    }
    CHECK(child_producer != nullptr);
    rows.push_back({std::move(child_producer), lines_per_child_[index]});
  }

  return std::make_unique<HorizontalSplitOutputProducer>(std::move(rows),
                                                         active_);
}

void BufferTreeHorizontal::SetSize(size_t lines, size_t columns) {
  lines_ = lines;
  columns_ = columns;
  RecomputeLinesPerChild();
}

size_t BufferTreeHorizontal::lines() const { return lines_; }

size_t BufferTreeHorizontal::columns() const { return columns_; }

size_t BufferTreeHorizontal::MinimumLines() {
  size_t count = 0;
  for (auto& child : children_) {
    static const int kFrameLines = 1;
    count += child->MinimumLines() + kFrameLines;
  }
  return count;
}

void BufferTreeHorizontal::RemoveBuffer(OpenBuffer* buffer) {
  for (auto& child : children_) {
    child->RemoveBuffer(buffer);
  }
}

Widget* BufferTreeHorizontal::Child() { return children_[active_].get(); }

void BufferTreeHorizontal::SetChild(std::unique_ptr<Widget> widget) {
  children_[active_] = std::move(widget);
}

void BufferTreeHorizontal::WrapChild(
    std::function<std::unique_ptr<Widget>(std::unique_ptr<Widget>)> callback) {
  children_[active_] = callback(std::move(children_[active_]));
}

size_t BufferTreeHorizontal::count() const { return children_.size(); }

size_t BufferTreeHorizontal::index() const { return active_; }

void BufferTreeHorizontal::set_index(size_t position) {
  CHECK(!children_.empty());
  active_ = position % children_.size();
}

size_t BufferTreeHorizontal::CountLeaves() const {
  int count = 0;
  for (const auto& child : children_) {
    count += child->CountLeaves();
  }
  return count;
}

int BufferTreeHorizontal::AdvanceActiveLeafWithoutWrapping(int delta) {
  LOG(INFO) << "BufferTreeHorizontal advances leaf: " << delta;
  while (delta > 0) {
    delta = children_[active_]->AdvanceActiveLeafWithoutWrapping(delta);
    if (active_ == children_.size() - 1) {
      return delta;
    } else if (delta > 0) {
      delta--;
      active_++;
    }
  }
  return delta;
}

void BufferTreeHorizontal::SetActiveLeavesAtStart() {
  active_ = 0;
  children_[active_]->SetActiveLeavesAtStart();
}

void BufferTreeHorizontal::RemoveActiveLeaf() {
  CHECK_LT(active_, children_.size());
  if (children_.size() == 1) {
    children_[0] = BufferWidget::New(std::weak_ptr<OpenBuffer>());
  } else {
    children_.erase(children_.begin() + active_);
    active_ %= children_.size();
  }
  CHECK_LT(active_, children_.size());
  RecomputeLinesPerChild();
}

void BufferTreeHorizontal::AddChild(std::unique_ptr<Widget> widget) {
  children_.push_back(std::move(widget));
  set_index(children_.size() - 1);
  RecomputeLinesPerChild();
}

void BufferTreeHorizontal::RecomputeLinesPerChild() {
  lines_per_child_.clear();
  for (size_t i = 0; i < children_.size(); i++) {
    auto child = children_[i].get();
    CHECK(child != nullptr);
    int lines = child->MinimumLines();
    lines_per_child_.push_back(lines);
  }
  CHECK_EQ(lines_per_child_.size(), children_.size());

  bool show_frames =
      std::count_if(lines_per_child_.begin(), lines_per_child_.end(),
                    [](size_t i) { return i > 0; }) > 1;
  if (show_frames) {
    LOG(INFO) << "Adding lines for frames.";
    for (auto& lines : lines_per_child_) {
      if (lines > 0) {
        static const int kFrameLines = 1;
        lines += kFrameLines;
      }
    }
  }

  size_t lines_given =
      std::accumulate(lines_per_child_.begin(), lines_per_child_.end(), 0);

  // TODO: this could be done way faster (sort + single pass over all
  // buffers).
  while (lines_given > lines_) {
    LOG(INFO) << "Ensuring that lines given (" << lines_given
              << ") doesn't exceed lines available (" << lines_ << ").";
    std::vector<size_t> indices_maximal_producers = {0};
    for (size_t i = 1; i < lines_per_child_.size(); i++) {
      size_t maximum = lines_per_child_[indices_maximal_producers.front()];
      if (maximum < lines_per_child_[i]) {
        indices_maximal_producers = {i};
      } else if (maximum == lines_per_child_[i]) {
        indices_maximal_producers.push_back(i);
      }
    }
    CHECK(!indices_maximal_producers.empty());
    CHECK_GT(lines_per_child_[indices_maximal_producers[0]], 0ul);
    for (auto& i : indices_maximal_producers) {
      if (lines_given > lines_) {
        lines_given--;
        lines_per_child_[i]--;
      }
    }
  }

  CHECK_EQ(lines_given, std::accumulate(lines_per_child_.begin(),
                                        lines_per_child_.end(), 0ul));

  if (lines_ > lines_given) {
    size_t lines_each = (lines_ - lines_given) / lines_per_child_.size();
    lines_given += lines_per_child_.size() * lines_each;
    for (auto& l : lines_per_child_) {
      size_t extra_line = lines_given < lines_ ? 1 : 0;
      l += lines_each + extra_line;
      lines_given += extra_line;
    }
  }

  CHECK_EQ(lines_, std::accumulate(lines_per_child_.begin(),
                                   lines_per_child_.end(), 0ul));

  for (size_t i = 0; i < lines_per_child_.size(); i++) {
    children_[i]->SetSize(lines_per_child_[i] - (show_frames ? 1 : 0),
                          columns_);
  }
}

}  // namespace editor
}  // namespace afc
