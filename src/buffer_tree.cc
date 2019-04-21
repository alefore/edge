#include "src/buffer_tree.h"

#include <glog/logging.h>

#include <algorithm>
#include <cctype>
#include <iostream>

namespace afc {
namespace editor {
namespace {
size_t CountLeafs(const BufferTree& tree) {
  switch (tree.type) {
    case BufferTree::Type::kLeaf:
      return 1;
    case BufferTree::Type::kVertical:
      int count = 0;
      for (auto& c : tree.children) {
        count += CountLeafs(c);
      }
      return count;
  }
  LOG(FATAL);
  return 0;
}

int InternalAdvanceActiveLeaf(BufferTree* tree, int delta) {
  LOG(INFO) << "Enter: " << delta << " " << *tree;
  switch (tree->type) {
    case BufferTree::Type::kLeaf:
      return delta;

    case BufferTree::Type::kVertical:
      delta = InternalAdvanceActiveLeaf(&tree->children[tree->active], delta);
      while (delta != 0) {
        if (tree->children.empty()) {
          return delta;
        }
        if (delta > 0) {
          if (tree->active == tree->children.size() - 1) {
            return delta;
          }
          tree->active++;
          delta--;
        }
      }
      return 0;
  }
  LOG(FATAL);
  return 0;
}
}  // namespace

/* static */ void BufferTree::RemoveActiveLeaf(BufferTree* tree) {
  auto route = FindRouteToActiveLeaf(tree);
  while (route.size() > 1 && route[route.size() - 2]->children.size() == 1) {
    BufferTree tmp = *route.back();
    route.pop_back();
    *route.back() = tmp;
  }
  if (route.size() > 1) {
    auto parent = route[route.size() - 2];
    parent->children.erase(parent->children.begin() + parent->active);
    parent->active %= parent->children.size();
  }
}

std::ostream& operator<<(std::ostream& os, const BufferTree& lc) {
  os << "[buffer tree ";
  switch (lc.type) {
    case BufferTree::Type::kLeaf:
      os << "leaf";
      break;
    case BufferTree::Type::kVertical:
      os << "vertical, children: " << lc.children.size()
         << ", active: " << lc.active;
  }
  os << "]";
  return os;
}

std::vector<BufferTree*> FindRouteToActiveLeaf(BufferTree* tree) {
  std::vector<BufferTree*> output = {tree};
  while (tree->type != BufferTree::Type::kLeaf) {
    CHECK(!tree->children.empty());
    tree = &tree->children[tree->active];
    output.push_back(tree);
  }
  return output;
}

BufferTree* FindActiveLeaf(BufferTree* tree) {
  return FindRouteToActiveLeaf(tree).back();
}

const BufferTree* FindActiveLeaf(const BufferTree* tree) {
  return FindRouteToActiveLeaf(const_cast<BufferTree*>(tree)).back();
}

void RemoveActiveLeaf(BufferTree* tree) {
  auto route = FindRouteToActiveLeaf(tree);
  while (route.size() > 1) {
    auto parent = route[route.size() - 2];
    if (parent->children.size() > 1) {
      parent->children.erase(parent->children.begin() + parent->active);
    }
  }
}

void AdvanceActiveLeaf(BufferTree* tree, int delta) {
  size_t leafs = CountLeafs(*tree);
  LOG(INFO) << "Advance with delta: " << delta << " " << *tree << " leafs "
            << leafs;
  if (delta < 0) {
    delta = leafs - ((-delta) % leafs);
  } else {
    delta %= leafs;
  }
  delta = InternalAdvanceActiveLeaf(tree, delta);
  LOG(INFO) << "After initial excursion: " << delta << " " << *tree;
  if (delta > 0) {
    BufferTree* tmp = tree;
    while (tmp->type == BufferTree::Type::kVertical) {
      tmp->active = 0;
      tmp = &tmp->children.front();
    }
    delta--;
    InternalAdvanceActiveLeaf(tree, delta);
  }
}
}  // namespace editor
}  // namespace afc
