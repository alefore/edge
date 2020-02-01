#include "src/widget_list.h"

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
#include "src/vertical_split_output_producer.h"
#include "src/widget.h"
#include "src/wstring.h"

namespace afc {
namespace editor {

BufferWidget* WidgetList::GetActiveLeaf() {
  return const_cast<BufferWidget*>(
      const_cast<const WidgetList*>(this)->GetActiveLeaf());
}

const BufferWidget* WidgetList::GetActiveLeaf() const {
  CHECK(!children_.empty());
  CHECK_LT(active_, children_.size());
  CHECK(children_[active_] != nullptr);
  return children_[active_]->GetActiveLeaf();
}

void WidgetList::ForEachBufferWidget(
    std::function<void(BufferWidget*)> callback) {
  for (auto& widget : children_) {
    widget->ForEachBufferWidget(callback);
  }
}
void WidgetList::ForEachBufferWidgetConst(
    std::function<void(const BufferWidget*)> callback) const {
  for (const auto& widget : children_) {
    widget->ForEachBufferWidgetConst(callback);
  }
}

void WidgetList::RemoveBuffer(OpenBuffer* buffer) {
  for (auto& child : children_) {
    child->RemoveBuffer(buffer);
  }
}

size_t WidgetList::count() const { return children_.size(); }

size_t WidgetList::index() const { return active_; }

void WidgetList::set_index(size_t position) {
  CHECK(!children_.empty());
  active_ = position % children_.size();
}

void WidgetList::AddChild(std::unique_ptr<Widget> widget) {
  children_.push_back(std::move(widget));
  set_index(children_.size() - 1);
}

Widget* WidgetList::Child() { return children_[active_].get(); }

void WidgetList::SetChild(std::unique_ptr<Widget> widget) {
  children_[active_] = std::move(widget);
}

void WidgetList::WrapChild(
    std::function<std::unique_ptr<Widget>(std::unique_ptr<Widget>)> callback) {
  children_[active_] = callback(std::move(children_[active_]));
}

size_t WidgetList::CountLeaves() const {
  int count = 0;
  for (const auto& child : children_) {
    count += child->CountLeaves();
  }
  return count;
}

int WidgetList::AdvanceActiveLeafWithoutWrapping(int delta) {
  LOG(INFO) << "WidgetList advances leaf: " << delta;
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

void WidgetList::SetActiveLeavesAtStart() {
  active_ = 0;
  children_[active_]->SetActiveLeavesAtStart();
}

void WidgetList::RemoveActiveLeaf() {
  CHECK_LT(active_, children_.size());
  if (children_.size() == 1) {
    children_[0] = BufferWidget::New(std::weak_ptr<OpenBuffer>());
  } else {
    children_.erase(children_.begin() + active_);
    active_ %= children_.size();
  }
  CHECK_LT(active_, children_.size());
}

WidgetList::WidgetList(std::vector<std::unique_ptr<Widget>> children,
                       size_t active)
    : children_(std::move(children)), active_(active) {}

WidgetList::WidgetList(std::unique_ptr<Widget> children)
    : WidgetList(
          [&]() {
            std::vector<std::unique_ptr<Widget>> output;
            output.push_back(std::move(children));
            return output;
          }(),
          0) {}

WidgetListHorizontal::WidgetListHorizontal(std::unique_ptr<Widget> children)
    : WidgetList(
          [&]() {
            std::vector<std::unique_ptr<Widget>> output;
            output.push_back(std::move(children));
            return output;
          }(),
          0) {}

WidgetListHorizontal::WidgetListHorizontal(
    std::vector<std::unique_ptr<Widget>> children, size_t active)
    : WidgetList(std::move(children), active) {}

wstring WidgetListHorizontal::Name() const { return L""; }

wstring WidgetListHorizontal::ToString() const {
  wstring output = L"[buffer tree horizontal, children: " +
                   std::to_wstring(children_.size()) + L", active: " +
                   std::to_wstring(active_) + L"]";
  return output;
}

std::unique_ptr<OutputProducer> WidgetListHorizontal::CreateOutputProducer(
    OutputProducerOptions options) const {
  std::vector<LineNumberDelta> lines_per_child;
  for (auto& child : children_) {
    lines_per_child.push_back(child->MinimumLines());
  }

  if (children_.size() > 1) {
    LOG(INFO) << "Adding lines for frames.";
    for (auto& lines : lines_per_child) {
      if (lines > LineNumberDelta(0)) {
        static const LineNumberDelta kFrameLines(1);
        lines += kFrameLines;
      }
    }
  }

  LineNumberDelta lines_given = std::accumulate(
      lines_per_child.begin(), lines_per_child.end(), LineNumberDelta(0));

  // TODO: this could be done way faster (sort + single pass over all
  // buffers).
  while (lines_given > options.size.line) {
    LOG(INFO) << "Ensuring that lines given (" << lines_given
              << ") doesn't exceed lines available (" << options.size.line
              << ").";
    std::vector<size_t> indices_maximal_producers = {0};
    for (size_t i = 1; i < lines_per_child.size(); i++) {
      LineNumberDelta maximum =
          lines_per_child[indices_maximal_producers.front()];
      if (maximum < lines_per_child[i]) {
        indices_maximal_producers = {i};
      } else if (maximum == lines_per_child[i]) {
        indices_maximal_producers.push_back(i);
      }
    }
    CHECK(!indices_maximal_producers.empty());
    CHECK_GT(lines_per_child[indices_maximal_producers[0]], LineNumberDelta(0));
    for (auto& i : indices_maximal_producers) {
      if (lines_given > options.size.line) {
        lines_given--;
        lines_per_child[i]--;
      }
    }
  }

  CHECK_EQ(lines_given,
           std::accumulate(lines_per_child.begin(), lines_per_child.end(),
                           LineNumberDelta(0)));

  if (options.size.line > lines_given) {
    LineNumberDelta lines_each =
        (options.size.line - lines_given) / lines_per_child.size();
    lines_given += lines_per_child.size() * lines_each;
    for (auto& l : lines_per_child) {
      LineNumberDelta extra_line = lines_given < options.size.line
                                       ? LineNumberDelta(1)
                                       : LineNumberDelta(0);
      l += lines_each + extra_line;
      lines_given += extra_line;
    }
  }

  CHECK_EQ(options.size.line,
           std::accumulate(lines_per_child.begin(), lines_per_child.end(),
                           LineNumberDelta(0)));

  std::vector<HorizontalSplitOutputProducer::Row> rows;
  CHECK_EQ(children_.size(), lines_per_child.size());
  for (size_t index = 0; index < children_.size(); index++) {
    std::shared_ptr<const OpenBuffer> buffer =
        children_[index]->GetActiveLeaf()->Lock();
    OutputProducerOptions child_options = options;
    child_options.size.line = lines_per_child[index];
    std::unique_ptr<OutputProducer> child_producer;
    if (children_.size() > 1) {
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
        frame_options.extra_information =
            OpenBuffer::FlagsToString(buffer->Flags());
      }
      nested_rows.push_back(
          {std::make_unique<FrameOutputProducer>(std::move(frame_options)),
           LineNumberDelta(1)});
      child_options.size.line -= nested_rows.back().lines;
      child_options.main_cursor_behavior =
          index == active_
              ? options.main_cursor_behavior
              : Widget::OutputProducerOptions::MainCursorBehavior::kHighlight;
      nested_rows.push_back(
          {children_[index]->CreateOutputProducer(child_options),
           child_options.size.line});
      child_producer = std::make_unique<HorizontalSplitOutputProducer>(
          std::move(nested_rows), 1);
    } else {
      child_producer = children_[index]->CreateOutputProducer(child_options);
    }
    CHECK(child_producer != nullptr);
    rows.push_back({std::move(child_producer), lines_per_child[index]});
  }

  return std::make_unique<HorizontalSplitOutputProducer>(std::move(rows),
                                                         active_);
}

LineNumberDelta WidgetListHorizontal::MinimumLines() const {
  LineNumberDelta count;
  for (auto& child : children_) {
    static const LineNumberDelta kFrameLines = LineNumberDelta(1);
    count += child->MinimumLines() + kFrameLines;
  }
  return count;
}

WidgetListVertical::WidgetListVertical(std::unique_ptr<Widget> children)
    : WidgetList(std::move(children)) {}

WidgetListVertical::WidgetListVertical(
    std::vector<std::unique_ptr<Widget>> children, size_t active)
    : WidgetList(std::move(children), active) {}

wstring WidgetListVertical::Name() const { return L""; }

wstring WidgetListVertical::ToString() const {
  wstring output = L"[buffer tree vertical, children: " +
                   std::to_wstring(children_.size()) + L", active: " +
                   std::to_wstring(active_) + L"]";
  return output;
}

std::unique_ptr<OutputProducer> WidgetListVertical::CreateOutputProducer(
    OutputProducerOptions options) const {
  std::vector<VerticalSplitOutputProducer::Column> columns(children_.size());

  ColumnNumberDelta base_columns = options.size.column / children_.size();
  ColumnNumberDelta columns_left =
      options.size.column - base_columns * children_.size();
  CHECK_LT(columns_left, ColumnNumberDelta(children_.size()));
  for (auto& column : columns) {
    column.width = base_columns;
    if (columns_left > ColumnNumberDelta(0)) {
      ++*column.width;
      --columns_left;
    }
  }
  CHECK_EQ(columns_left, ColumnNumberDelta(0));

  for (size_t index = 0; index < children_.size(); index++) {
    auto& column = columns[index];
    OutputProducerOptions child_options = options;
    CHECK(column.width.has_value());
    child_options.size.column = column.width.value();
    child_options.main_cursor_behavior =
        index == active_
            ? options.main_cursor_behavior
            : Widget::OutputProducerOptions::MainCursorBehavior::kHighlight;
    column.producer =
        children_[index]->CreateOutputProducer(std::move(child_options));
    CHECK(column.producer != nullptr);
  }

  return std::make_unique<VerticalSplitOutputProducer>(std::move(columns),
                                                       active_);
}

LineNumberDelta WidgetListVertical::MinimumLines() const {
  LineNumberDelta output = children_[0]->MinimumLines();
  for (auto& child : children_) {
    output = max(child->MinimumLines(), output);
  }
  static const LineNumberDelta kFrameLines = LineNumberDelta(1);
  return output + kFrameLines;
}

}  // namespace editor
}  // namespace afc
