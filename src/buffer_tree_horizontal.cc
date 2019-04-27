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
#include "src/dirname.h"
#include "src/frame_output_producer.h"
#include "src/horizontal_split_output_producer.h"
#include "src/widget.h"
#include "src/wstring.h"

namespace afc {
namespace editor {
struct BufferListPosition {
  std::shared_ptr<OpenBuffer> buffer;
  size_t index;
};

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

class BuffersListProducer : public OutputProducer {
 public:
  static const size_t kMinimumColumnsPerBuffer = 20;

  BuffersListProducer(std::vector<std::vector<BufferListPosition>> buffers)
      : buffers_(buffers),
        max_index_([&]() {
          size_t output = buffers_[0][0].index;
          for (auto& line : buffers_) {
            for (auto& buffer : line) {
              output = max(buffer.index, output);
            }
          }
          return output;
        }()),
        prefix_width_(std::to_wstring(max_index_ + 1).size() + 2) {}

  void WriteLine(Options options) override {
    size_t columns_per_buffer =  // Excluding prefixes and separators.
        (options.receiver->width() -
         min(options.receiver->width(), (prefix_width_ * buffers_[0].size()))) /
        buffers_[0].size();
    for (size_t i = 0; i < buffers_[current_line_].size(); i++) {
      auto buffer = buffers_[current_line_][i].buffer;
      options.receiver->AddModifier(LineModifier::RESET);
      auto name =
          buffer == nullptr ? L"(dead)" : buffer->Read(buffer_variables::name);
      auto number_prefix =
          std::to_wstring(buffers_[current_line_][i].index + 1);
      size_t start = i * (columns_per_buffer + prefix_width_) +
                     (prefix_width_ - number_prefix.size() - 2);
      if (options.receiver->column() < start) {
        options.receiver->AddString(
            wstring(start - options.receiver->column(), L' '));
      }
      options.receiver->AddModifier(LineModifier::CYAN);
      options.receiver->AddString(number_prefix);
      options.receiver->AddModifier(LineModifier::RESET);

      std::list<std::wstring> output_components;
      std::list<std::wstring> components;
      if (buffer != nullptr && buffer->Read(buffer_variables::path) == name &&
          DirectorySplit(name, &components) && !components.empty()) {
        name.clear();
        output_components.push_front(components.back());
        if (output_components.front().size() > columns_per_buffer) {
          output_components.front() = output_components.front().substr(
              output_components.front().size() - columns_per_buffer);
        } else {
          size_t consumed = output_components.front().size();
          components.pop_back();

          static const size_t kSizeOfSlash = 1;
          while (!components.empty()) {
            if (columns_per_buffer >
                components.size() * 2 + components.back().size() + consumed) {
              output_components.push_front(components.back());
              consumed += components.back().size() + kSizeOfSlash;
            } else if (columns_per_buffer > 1 + kSizeOfSlash + consumed) {
              output_components.push_front(
                  std::wstring(1, components.back()[0]));
              consumed += 1 + kSizeOfSlash;
            } else {
              break;
            }
            components.pop_back();
          }
        }
      }

      options.receiver->AddModifier(LineModifier::DIM);
      if (!name.empty()) {
        if (name.size() > columns_per_buffer) {
          name = name.substr(name.size() - columns_per_buffer);
          options.receiver->AddString(L"…");
        } else {
          options.receiver->AddString(L":");
        }
        options.receiver->AddModifier(LineModifier::RESET);
        options.receiver->AddString(name);
        continue;
      }

      if (components.empty()) {
        options.receiver->AddString(L":");
      } else {
        options.receiver->AddString(L"…");
      }
      options.receiver->AddModifier(LineModifier::RESET);

      auto last = output_components.end();
      --last;
      for (auto it = output_components.begin(); it != output_components.end();
           ++it) {
        if (it != output_components.begin()) {
          options.receiver->AddModifier(LineModifier::DIM);
          options.receiver->AddCharacter(L'/');
          options.receiver->AddModifier(LineModifier::RESET);
        }
        if (it == last) {
          options.receiver->AddModifier(LineModifier::BOLD);
        }
        options.receiver->AddString(*it);
      }
      options.receiver->AddModifier(LineModifier::RESET);
    }
    current_line_++;
  }

 private:
  const std::vector<std::vector<BufferListPosition>> buffers_;
  const size_t max_index_;
  const size_t prefix_width_;
  int current_line_ = 0;
};

std::unique_ptr<OutputProducer> BufferTreeHorizontal::CreateOutputProducer() {
  std::vector<HorizontalSplitOutputProducer::Row> rows;

  bool show_frames =
      std::count_if(lines_per_child_.begin(), lines_per_child_.end(),
                    [](size_t i) { return i > 0; }) > 1;

  for (size_t index = 0; index < children_.size(); index++) {
    auto child_producer = children_[index]->CreateOutputProducer();
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
    rows.push_back({std::move(child_producer), lines_per_child_[index]});
  }

  rows.push_back({std::make_unique<BuffersListProducer>(buffers_list_),
                  buffers_list_.size()});
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

void BufferTreeHorizontal::SetActiveLeaf(size_t position) {
  CHECK(!children_.empty());
  active_ = position % children_.size();
}

// Returns the number of steps advanced.
void BufferTreeHorizontal::AdvanceActiveLeaf(int delta) {
  size_t leafs = CountLeafs();
  if (delta < 0) {
    delta = leafs - ((-delta) % leafs);
  } else {
    delta %= leafs;
  }
  delta = AdvanceActiveLeafWithoutWrapping(delta);
  if (delta > 0) {
    auto tmp = this;
    while (tmp != nullptr) {
      tmp->active_ = 0;
      tmp = dynamic_cast<BufferTreeHorizontal*>(tmp->children_[0].get());
    }
    delta--;
  }
  AdvanceActiveLeafWithoutWrapping(delta);
}

int BufferTreeHorizontal::AdvanceActiveLeafWithoutWrapping(int delta) {
  while (delta > 0) {
    auto child = dynamic_cast<BufferTreeHorizontal*>(children_[active_].get());
    if (child != nullptr) {
      delta -= child->AdvanceActiveLeafWithoutWrapping(delta);
    }
    if (active_ == children_.size() - 1) {
      return delta;
    } else if (delta > 0) {
      delta--;
      active_++;
    }
  }
  return delta;
}

size_t BufferTreeHorizontal::CountLeafs() const {
  int count = 0;
  for (const auto& child : children_) {
    auto casted_child = dynamic_cast<const BufferTreeHorizontal*>(child.get());
    count += casted_child == nullptr ? 1 : casted_child->CountLeafs();
  }
  return count;
}

BufferTreeHorizontal::LeafSearchResult BufferTreeHorizontal::SelectLeafFor(
    OpenBuffer* buffer) {
  for (size_t i = 0; i < children_.size(); i++) {
    auto casted = dynamic_cast<BufferTreeHorizontal*>(children_[i].get());
    LeafSearchResult result = LeafSearchResult::kNotFound;
    if (casted == nullptr) {
      if (children_[i]->GetActiveLeaf()->Lock().get() == buffer) {
        result = LeafSearchResult::kFound;
      }
    } else {
      result = casted->SelectLeafFor(buffer);
    }
    if (result == LeafSearchResult::kFound) {
      active_ = i;
      return result;
    }
  }
  return LeafSearchResult::kNotFound;
}

void BufferTreeHorizontal::InsertChildren(std::shared_ptr<OpenBuffer> buffer,
                                          InsertionType insertion_type) {
  switch (insertion_type) {
    case InsertionType::kSearchOrCreate:
      if (SelectLeafFor(buffer.get()) == LeafSearchResult::kFound) {
        RecomputeLinesPerChild();
        return;
      }
      // Fallthrough.

    case InsertionType::kCreate:
      children_.push_back(BufferWidget::New(std::move(buffer)));
      SetActiveLeaf(children_count() - 1);
      RecomputeLinesPerChild();
      break;

    case InsertionType::kReuseCurrent:
      GetActiveLeaf()->SetBuffer(buffer);
      RecomputeLinesPerChild();
      break;

    case InsertionType::kSkip:
      break;
  }
}

void BufferTreeHorizontal::PushChildren(std::unique_ptr<Widget> children) {
  children_.push_back(std::move(children));
  RecomputeLinesPerChild();
}

size_t BufferTreeHorizontal::children_count() const { return children_.size(); }

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

void BufferTreeHorizontal::AddSplit() {
  InsertChildren(nullptr, InsertionType::kCreate);
  RecomputeLinesPerChild();
}

void BufferTreeHorizontal::ZoomToActiveLeaf() {
  children_[0] = std::move(children_[active_]);
  children_.resize(1);
  active_ = 0;
  RecomputeLinesPerChild();
}

BufferTreeHorizontal::BuffersVisible BufferTreeHorizontal::buffers_visible()
    const {
  return buffers_visible_;
}

void BufferTreeHorizontal::SetBuffersVisible(BuffersVisible buffers_visible) {
  buffers_visible_ = buffers_visible;
  RecomputeLinesPerChild();
}

void BufferTreeHorizontal::RecomputeLinesPerChild() {
  lines_per_child_.clear();
  for (size_t i = 0; i < children_.size(); i++) {
    auto child = children_[i].get();
    CHECK(child != nullptr);
    int lines = 0;
    switch (buffers_visible_) {
      case BuffersVisible::kActive:
        lines = i == active_ ? lines_ : 0;
        break;
      case BuffersVisible::kAll:
        lines = max(child->MinimumLines(), i == active_ ? 1ul : 0ul);
    }
    lines_per_child_.push_back(lines);
  }

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

  std::vector<BufferListPosition> all_buffers;
  for (size_t i = 0; i < lines_per_child_.size(); i++) {
    all_buffers.push_back({children_[i]->GetActiveLeaf()->Lock(), i});
  }

  size_t buffers_list_lines =
      ceil(static_cast<double>(all_buffers.size() *
                               BuffersListProducer::kMinimumColumnsPerBuffer) /
           columns_);
  size_t buffers_list_buffers_per_line =
      ceil(static_cast<double>(all_buffers.size()) / buffers_list_lines);
  buffers_list_.clear();
  for (auto& buffer : all_buffers) {
    if (buffers_list_.empty() ||
        buffers_list_.back().size() >= buffers_list_buffers_per_line) {
      buffers_list_.push_back({});
    }
    buffers_list_.back().push_back(std::move(buffer));
  }

  size_t reserved_lines = buffers_list_.size();

  // TODO: this could be done way faster (sort + single pass over all
  // buffers).
  while (lines_given > lines_ - reserved_lines) {
    LOG(INFO) << "Ensuring that lines given (" << lines_given
              << ") doesn't exceed lines available (" << lines_ << " - "
              << reserved_lines << ").";
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
    CHECK_GT(lines_per_child_[indices_maximal_producers[0]], 0);
    for (auto& i : indices_maximal_producers) {
      if (lines_given > lines_ - reserved_lines) {
        lines_given--;
        lines_per_child_[i]--;
      }
    }
  }

  if (lines_given < lines_ - reserved_lines) {
    LOG(INFO) << "Donating spare lines to the active widget: "
              << lines_ - reserved_lines - lines_given;
    lines_per_child_[active_] += lines_ - reserved_lines - lines_given;
  }
  for (size_t i = 0; i < lines_per_child_.size(); i++) {
    if (buffers_visible_ == BuffersVisible::kActive && i != active_) {
      continue;
    }
    children_[i]->SetSize(lines_per_child_[i] - (show_frames ? 1 : 0),
                          columns_);
  }
}  // namespace editor

}  // namespace editor
}  // namespace afc
