#include "src/buffer_tree_horizontal.h"

#include <glog/logging.h>

#include <algorithm>
#include <cctype>
#include <iostream>

#include "src/buffer.h"
#include "src/buffer_output_producer.h"
#include "src/buffer_tree.h"
#include "src/buffer_tree_leaf.h"
#include "src/buffer_variables.h"
#include "src/framed_output_producer.h"
#include "src/horizontal_split_output_producer.h"
#include "src/wstring.h"

namespace afc {
namespace editor {
/* static */ std::unique_ptr<BufferTreeHorizontal> BufferTreeHorizontal::New(
    std::vector<std::unique_ptr<BufferTree>> children, size_t active) {
  return std::make_unique<BufferTreeHorizontal>(ConstructorAccessTag(),
                                                std::move(children), active);
}

/* static */
std::unique_ptr<BufferTree> BufferTreeHorizontal::AddHorizontalSplit(
    std::unique_ptr<BufferTree> tree) {
  auto casted_tree = dynamic_cast<BufferTreeHorizontal*>(tree.get());
  if (casted_tree == nullptr) {
    std::vector<std::unique_ptr<BufferTree>> children;
    children.emplace_back(std::move(tree));
    tree = BufferTreeHorizontal::New(std::move(children), 0);
    casted_tree = dynamic_cast<BufferTreeHorizontal*>(tree.get());
    CHECK(casted_tree != nullptr);
  }

  casted_tree->PushChildren(BufferTreeLeaf::New(std::weak_ptr<OpenBuffer>()));
  casted_tree->SetActiveLeaf(casted_tree->children_count() - 1);
  return tree;
}

/* static */
std::unique_ptr<BufferTree> BufferTreeHorizontal::RemoveActiveLeaf(
    std::unique_ptr<BufferTree> tree) {
  tree = RemoveActiveLeafInternal(std::move(tree));
  return tree == nullptr ? BufferTreeLeaf::New(std::shared_ptr<OpenBuffer>())
                         : std::move(tree);
}

BufferTreeHorizontal::BufferTreeHorizontal(
    ConstructorAccessTag, std::vector<std::unique_ptr<BufferTree>> children,
    size_t active)
    : children_(std::move(children)), active_(active) {}

BufferTreeLeaf* BufferTreeHorizontal::GetActiveLeaf() {
  return children_[active_]->GetActiveLeaf();
}

void BufferTreeHorizontal::SetActiveLeaf(size_t position) {
  CHECK(!children_.empty());
  CHECK_LT(position, children_.size());
  active_ = position;
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
  for (const auto& c : children_) {
    count += c->CountLeafs();
  }
  return count;
}

wstring BufferTreeHorizontal::Name() const { return L""; }

wstring BufferTreeHorizontal::ToString() const {
  wstring output = L"[buffer tree horizontal, children: " +
                   std::to_wstring(children_.size()) + L", active: " +
                   std::to_wstring(active_) + L"]";
  return output;
}

std::unique_ptr<OutputProducer> BufferTreeHorizontal::CreateOutputProducer() {
  std::vector<std::unique_ptr<OutputProducer>> output_producers;
  for (size_t index = 0; index < children_.size(); index++) {
    output_producers.push_back(std::make_unique<FramedOutputProducer>(
        children_[index]->CreateOutputProducer(), children_[index]->Name(),
        index));
  }
  return std::make_unique<HorizontalSplitOutputProducer>(
      std::move(output_producers), lines_per_child_, active_);
}

void BufferTreeHorizontal::PushChildren(std::unique_ptr<BufferTree> children) {
  children_.push_back(std::move(children));
}

size_t BufferTreeHorizontal::children_count() const { return children_.size(); }

/* static */
std::unique_ptr<BufferTree> BufferTreeHorizontal::RemoveActiveLeafInternal(
    std::unique_ptr<BufferTree> tree) {
  auto casted_tree = dynamic_cast<BufferTreeHorizontal*>(tree.get());
  if (casted_tree == nullptr) {
    return nullptr;  // It's a leaf, remove it.
  }

  casted_tree->children_[casted_tree->active_] = RemoveActiveLeafInternal(
      std::move(casted_tree->children_[casted_tree->active_]));
  if (casted_tree->children_[casted_tree->active_] != nullptr) {
    return tree;  // Subtree isn't empty (post removal). We're done.
  }

  if (casted_tree->children_.size() == 1) {
    return nullptr;  // We've become empty, remove ourselves.
  }

  casted_tree->children_.erase(casted_tree->children_.begin() +
                               casted_tree->active_);
  casted_tree->active_ %= casted_tree->children_.size();
  return tree;
}

void BufferTreeHorizontal::SetLines(size_t lines) {
  lines_ = lines;
  RecomputeLinesPerChild();
}

size_t BufferTreeHorizontal::lines() const { return lines_; }

size_t BufferTreeHorizontal::MinimumLines() {
  size_t count = 0;
  for (auto& child : children_) {
    static const int kFrameLines = 1;
    count += child->MinimumLines() + kFrameLines;
  }
  return count;
}

void BufferTreeHorizontal::RecomputeLinesPerChild() {
  static const int kFrameLines = 1;

  size_t lines_given = 0;

  lines_per_child_.clear();
  for (auto& child : children_) {
    lines_per_child_.push_back(child->MinimumLines() + kFrameLines);
    lines_given += lines_per_child_.back();
  }

  // TODO: this could be done way faster (sort + single pass over all buffers).
  while (lines_given > lines_) {
    std::vector<size_t> indices_maximal_producers = {0};
    for (size_t i = 1; i < lines_per_child_.size(); i++) {
      size_t maximum = lines_per_child_[indices_maximal_producers.front()];
      if (maximum < lines_per_child_[i]) {
        indices_maximal_producers = {i};
      } else if (maximum == lines_per_child_[i]) {
        indices_maximal_producers.push_back(i);
      }
    }
    for (auto& i : indices_maximal_producers) {
      if (lines_given > lines_) {
        lines_given--;
        lines_per_child_[i]--;
      }
    }
  }

  if (lines_given < lines_) {
    lines_per_child_[active_] += lines_ - lines_given;
  }
  for (size_t i = 0; i < lines_per_child_.size(); i++) {
    children_[i]->SetLines(lines_per_child_[i] - kFrameLines);
  }
}

}  // namespace editor
}  // namespace afc
