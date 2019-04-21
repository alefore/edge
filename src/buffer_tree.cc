#include "src/buffer_tree.h"

#include <glog/logging.h>

#include <algorithm>
#include <cctype>
#include <iostream>

#include "src/wstring.h"

namespace afc {
namespace editor {
/* static */
BufferTree BufferTree::NewLeaf(std::weak_ptr<OpenBuffer> buffer) {
  BufferTree output;
  output.type_ = Type::kLeaf;
  output.leaf_ = buffer;
  return output;
}

/* static */
BufferTree BufferTree::NewHorizontal(Tree<BufferTree> children,
                                     size_t active_index) {
  CHECK(!children.empty());
  CHECK_LT(active_index, children.size());
  BufferTree output;
  output.type_ = Type::kHorizontal;
  output.children_ = std::move(children);
  output.active_ = active_index;
  return output;
}

void BufferTree::AddHorizontalSplit() {
  if (type_ != BufferTree::Type::kHorizontal) {
    BufferTree old_tree = *this;
    type_ = BufferTree::Type::kHorizontal;
    leaf_.reset();
    children_.clear();
    children_.push_back(std::move(old_tree));
    active_ = 0;
  }

  active_ = children_.size();
  children_.push_back(BufferTree());
}

void BufferTree::SetActiveLeafBuffer(std::shared_ptr<OpenBuffer> buffer) {
  switch (type_) {
    case Type::kLeaf:
      leaf_ = std::move(buffer);
      return;
    case Type::kHorizontal:
      children_[active_].SetActiveLeafBuffer(std::move(buffer));
      return;
  }
  LOG(FATAL);
}

void BufferTree::SetActiveLeaf(size_t position) {
  switch (type_) {
    case BufferTree::Type::kLeaf:
      break;
    case BufferTree::Type::kHorizontal:
      CHECK(!children_.empty());
      active_ = position % children_.size();
      break;
  }
}

std::shared_ptr<OpenBuffer> BufferTree::LockActiveLeaf() const {
  switch (type_) {
    case Type::kLeaf:
      return leaf_.lock();
    case Type::kHorizontal:
      return children_[active_].LockActiveLeaf();
  }
  LOG(FATAL);
  return 0;
}

void BufferTree::RemoveActiveLeaf() {
  auto route = FindRouteToActiveLeaf();
  while (route.size() > 1 && route[route.size() - 2]->children_.size() == 1) {
    BufferTree tmp = *route.back();
    route.pop_back();
    *route.back() = tmp;
  }
  if (route.size() > 1) {
    auto parent = route[route.size() - 2];
    parent->children_.erase(parent->children_.begin() + parent->active_);
    parent->active_ %= parent->children_.size();
  }
}

std::vector<BufferTree*> BufferTree::FindRouteToActiveLeaf() {
  std::vector<BufferTree*> output = {this};
  BufferTree* tree = this;
  while (tree->type_ != BufferTree::Type::kLeaf) {
    CHECK(!tree->children_.empty());
    tree = &tree->children_[tree->active_];
    output.push_back(tree);
  }
  return output;
}

BufferTree* BufferTree::FindActiveLeaf() {
  return FindRouteToActiveLeaf().back();
}

const BufferTree* BufferTree::FindActiveLeaf() const {
  return const_cast<BufferTree*>(this)->FindRouteToActiveLeaf().back();
}

void BufferTree::AdvanceActiveLeaf(int delta) {
  size_t leafs = CountLeafs();
  if (delta < 0) {
    delta = leafs - ((-delta) % leafs);
  } else {
    delta %= leafs;
  }
  delta = InternalAdvanceActiveLeaf(delta);
  if (delta > 0) {
    BufferTree* tmp = this;
    while (tmp->type_ == BufferTree::Type::kHorizontal) {
      tmp->active_ = 0;
      tmp = &tmp->children_.front();
    }
    delta--;
    InternalAdvanceActiveLeaf(delta);
  }
}

size_t BufferTree::CountLeafs() const {
  switch (type_) {
    case BufferTree::Type::kLeaf:
      return 1;
    case BufferTree::Type::kHorizontal:
      int count = 0;
      for (const auto& c : children_) {
        count += c.CountLeafs();
      }
      return count;
  }
  LOG(FATAL);
  return 0;
}

wstring BufferTree::ToString() const {
  wstring output = L"[buffer tree ";
  switch (type_) {
    case BufferTree::Type::kLeaf:
      output += L"leaf";
      break;
    case BufferTree::Type::kHorizontal:
      output += L"horizontal, children: " + std::to_wstring(children_.size()) +
                L", active: " + std::to_wstring(active_);
  }
  output += L"]";
  return output;
}

int BufferTree::InternalAdvanceActiveLeaf(int delta) {
  switch (type_) {
    case BufferTree::Type::kLeaf:
      return delta;

    case BufferTree::Type::kHorizontal:
      delta = children_[active_].InternalAdvanceActiveLeaf(delta);
      while (delta != 0) {
        if (children_.empty()) {
          return delta;
        }
        if (delta > 0) {
          if (active_ == children_.size() - 1) {
            return delta;
          }
          active_++;
          delta--;
        }
      }
      return 0;
  }
  LOG(FATAL);
  return 0;
}

std::ostream& operator<<(std::ostream& os, const BufferTree& lc) {
  os << lc.ToString();
  return os;
}

}  // namespace editor
}  // namespace afc
