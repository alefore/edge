#ifndef __AFC_EDITOR_TREE_H__
#define __AFC_EDITOR_TREE_H__

#include <algorithm>
#include <memory>

#include <glog/logging.h>

namespace afc {
namespace editor {

template <typename Item>
class Node;
template <typename Item>
class Tree;

template <typename Item>
std::ostream& operator<<(std::ostream& out, const Node<Item>& node);

template <typename Item>
inline std::ostream& operator<<(std::ostream& out, const Tree<Item>& tree);

template <bool flag, class IsTrue, class IsFalse>
struct choose;

template <class IsTrue, class IsFalse>
struct choose<true, IsTrue, IsFalse> {
  typedef IsTrue type;
};

template <class IsTrue, class IsFalse>
struct choose<false, IsTrue, IsFalse> {
  typedef IsFalse type;
};

template <typename Item, bool IsConst>
class TreeIterator
    : public std::iterator<std::random_access_iterator_tag, Item> {
 public:
  typedef std::forward_iterator_tag iterator_category;
  typedef Item value_type;
  typedef size_t difference_type;
  typedef typename choose<IsConst, const Item&, Item&>::type reference;
  typedef typename choose<IsConst, const Item*, Item*>::type pointer;

  typedef typename choose<IsConst, const Tree<Item>, Tree<Item>>::type TreeItem;
  typedef typename choose<IsConst, const Node<Item>, Node<Item>>::type NodeItem;

  TreeIterator() : TreeIterator(nullptr, nullptr) {}

  explicit TreeIterator(TreeItem* tree, NodeItem* node)
      : tree_(tree), node_(node) {}

  template <bool OtherIsConst>
  bool operator==(const TreeIterator<Item, OtherIsConst>& rhs) const;
  template <bool OtherIsConst>
  bool operator!=(const TreeIterator<Item, OtherIsConst>& rhs) const;

  TreeIterator<Item, IsConst> operator=(const TreeIterator<Item, IsConst>& rhs);

  TreeIterator<Item, IsConst>& operator++();
  TreeIterator<Item, IsConst>& operator--();

  TreeIterator<Item, IsConst>& operator+=(const int& num);
  TreeIterator<Item, IsConst>& operator-=(const int& num);
  TreeIterator<Item, IsConst> operator+(const int& num) const;
  TreeIterator<Item, IsConst> operator-(const int& num) const;
  int operator-(const TreeIterator<Item, true>& num) const;
  int operator-(const TreeIterator<Item, false>& num) const;

  template <bool OtherIsConst>
  bool operator<(const TreeIterator<Item, OtherIsConst>& other) const;
  template <bool OtherIsConst>
  bool operator<=(const TreeIterator<Item, OtherIsConst>& other) const;
  template <bool OtherIsConst>
  bool operator>(const TreeIterator<Item, OtherIsConst>& other) const;
  template <bool OtherIsConst>
  bool operator>=(const TreeIterator<Item, OtherIsConst>& other) const;

  reference operator*() const;

  operator TreeIterator<Item, true>() const;

 private:
  friend class Tree<Item>;
  friend class TreeIterator<Item, true>;
  friend class TreeIterator<Item, false>;
  TreeItem* tree_;
  NodeItem* node_;
};

template <typename Item>
class Tree {
 public:
  typedef Item value;
  typedef Item& reference;
  typedef const Item& const_reference;
  typedef TreeIterator<Item, false> iterator;
  typedef TreeIterator<Item, true> const_iterator;
  typedef std::reverse_iterator<iterator> reverse_iterator;
  typedef std::reverse_iterator<const_iterator> const_reverse_iterator;

  void clear();

  bool empty() const;
  reference at(size_t position);
  const_reference at(size_t position) const;

  reference operator[](std::size_t position) { return at(position); }
  const_reference operator[](std::size_t position) const {
    return at(position);
  };

  // Returns an iterator pointing to the first element in the Tree (or the end
  // if there are no elements).
  iterator begin();
  const_iterator cbegin();
  const_iterator begin() const;
  reverse_iterator rbegin();
  const_reverse_iterator rbegin() const;

  // Returns an iterator pointing to the end of the Tree (one after the last
  // element).
  iterator end();
  const_iterator cend();
  const_iterator end() const;

  size_t size() const;

  void push_back(const Item& item);

  template <class... Args>
  void emplace_back(Args&&... args);

  reference back();
  const_reference back() const;

  reference front();
  const_reference front() const;

  // Inserts an item at the position given. Items past that position are
  // shifted.
  void insert(const iterator& position, Item item);

  template <typename InputIterator>
  void insert(const iterator& position, InputIterator first,
              InputIterator last);

  iterator erase(iterator position);
  // TODO: Current implementation has linear runtime to end - start.
  iterator erase(iterator start, iterator end);

 private:
  static size_t Count(Node<Item>* node);
  static size_t Height(const Node<Item>* node);

  void ValidateInvariants() const;
  void ValidateInvariants(const Node<Item>* node) const;

  void RecomputeCounters(Node<Item>* node);

  // Rotates the tree to the right or to the left.
  template <Node<Item>* Node<Item>::*Left, Node<Item>* Node<Item>::* Right>
  void Rotate(Node<Item>* node);
  // Takes old out of a tree, replacing it with new.
  void ReplaceNode(const Node<Item>* old_node, Node<Item>* new_node);

  template <Node<Item>* Node<Item>::*Left, Node<Item>* Node<Item>::*Right>
  bool MaybeRotateLeft(Node<Item>* node, bool insert);

  // Given a node where a subtree has been modified, ensure that it remains
  // properly balanced. stop may be null, to rebalance all the way to the top
  // (or until we can ascertain that no further rebalancing is needed).
  //
  // Precondition: The invariants on any tree other than node or direct parents
  //     must hold. stop must be a superparent of node.
  // Postcondition: The invariants on the supertrees of node up to (excluding)
  //     stop hold.
  void MaybeRebalance(Node<Item>* node, const Node<Item>* stop, bool insert);

  // Insert node as the right child of parent. parent must not already have a
  // right child.
  void InsertRight(Node<Item>* parent, Node<Item>* node);

  // Inserts an element at level 0 at the position specified. node is the new
  // node to insert.
  void insert(const iterator& position, Node<Item>* node);

  // Returns the left-most element under node. If node is nullptr, just returns
  // it.
  const Node<Item>* FirstNode(const Node<Item>* node) const;
  Node<Item>* FirstNode(Node<Item>* node);
  const Node<Item>* LastNode(const Node<Item>* node) const;
  Node<Item>* LastNode(Node<Item>* node);

  // Deletes node and all its children recursively.
  void DeleteNodes(Node<Item>* node);
  size_t FindPosition(const Node<Item>* node) const;

  friend class TreeIterator<Item, false>;
  friend class TreeIterator<Item, true>;
  friend std::ostream& operator<< <>(std::ostream& out, const Tree<Item>& tree);
  friend std::ostream& operator<< <>(std::ostream& out, const Node<Item>& tree);

  Node<Item>* root_ = nullptr;
};

// --- Implementation details ---

template <typename Item>
std::ostream& operator<<(std::ostream& out, const Node<Item>& node) {
  out << "(" << node.item;
  if (node.left) { out << " l:" << *node.left; }
  if (node.right) { out << " r:" << *node.right; }
  out << ")";
  return out;
}

template <typename Item>
inline std::ostream& operator<<(std::ostream& out, const Tree<Item>& tree) {
  if (tree.root_ == nullptr) {
    out << "(empty tree)";
  } else {
    out << *tree.root_;
  }
  return out;
}

template <typename Item>
struct Node {
  typedef Item value;

  Node(Item item) : item(std::move(item)) {}

  void ValidateInvariants();

  size_t count = 1;
  int height = 1;

  Item item;

  Node<Item>* parent = nullptr;
  Node<Item>* left = nullptr;
  Node<Item>* right = nullptr;

  operator Node<const Item>() const {
    Node<const Item> node(item);
    node.count = count;
    node.height = height;
    node.parent = parent;
    node.left = left;
    node.right = right;
    return node;
  }
};

// ---- Functions for iterators ----

template <typename Item, bool IsConst>
template <bool OtherIsConst>
bool TreeIterator<Item, IsConst>::operator==(
    const TreeIterator<Item, OtherIsConst>& rhs) const {
  return node_ == rhs.node_;
}

template <typename Item, bool IsConst>
template <bool OtherIsConst>
bool TreeIterator<Item, IsConst>::operator!=(
    const TreeIterator<Item, OtherIsConst>& rhs) const {
  return !(*this == rhs);
}

template <typename Item, bool IsConst>
TreeIterator<Item, IsConst> TreeIterator<Item, IsConst>::operator=(
    const TreeIterator<Item, IsConst>& rhs) {
  this->tree_ = rhs.tree_;
  this->node_ = rhs.node_;
  return *this;
}

template <typename Item, bool IsConst>
TreeIterator<Item, IsConst>& TreeIterator<Item, IsConst>::operator++() {
  *this += 1;
  return *this;
}

template <typename Item, bool IsConst>
TreeIterator<Item, IsConst>& TreeIterator<Item, IsConst>::operator--() {
  *this -= 1;
  return *this;
}

template <typename Item>
/* static */ size_t Tree<Item>::Count(Node<Item>* node) {
  return node == nullptr ? 0 : node->count;
}

template <typename Item, bool IsConst>
TreeIterator<Item, IsConst>& TreeIterator<Item, IsConst>::operator+=(
    const int& original_delta) {
  size_t original_position = tree_->FindPosition(node_);
  int delta = original_delta;
  DVLOG(5) << "Start at " << original_position << " and advance " << delta
           << " with " << tree_->size();

  if (node_ == nullptr) {
    DCHECK_LE(delta, 0) << "Attempting to advance past end of tree.";
    node_ = tree_->LastNode(tree_->root_);
    if (node_ != nullptr) { delta++; }
    DVLOG(7) << "Adjusted: " << tree_->FindPosition(node_) << " and advance "
             << delta << " with " << tree_->size();
  } else {
    DVLOG(6) << "Now at: " << *node_;
  }

  // Go up one level in each iteration until we know we can go down.
  while (node_ != nullptr
         && ((delta > 0)
                  ? delta > Tree<Item>::Count(node_->right)
                  : -delta > Tree<Item>::Count(node_->left))) {
    if (node_->parent == nullptr || node_->parent->left == node_) {
      DVLOG(7) << "Going up through left branch";
      delta -= 1 + Tree<Item>::Count(node_->right);
    } else {
      DVLOG(7) << "Going up through right branch";
      delta += 1 + Tree<Item>::Count(node_->left);
    }
    node_ = node_->parent;
    DVLOG(7) << "After loop at " << tree_->FindPosition(node_)
             << " and advance " << delta << " with " << tree_->size();
  }

  tree_->ValidateInvariants(node_);
  // Now go down one level in each iteration.
  while (delta != 0) {
    DCHECK(node_ != nullptr);
    DVLOG(7) << "Before down at " << tree_->FindPosition(node_)
             << " and advance " << delta << " with " << tree_->size();
    if (delta > 0) {
      DVLOG(6) << "Down the right branch.";
      node_ = node_->right;
      delta = delta - 1 - Tree<Item>::Count(node_->left);
    } else {
      DVLOG(6) << "Down the left branch.";
      node_ = node_->left;
      delta = Tree<Item>::Count(node_->right) + delta + 1;
    }
    DVLOG(7) << "After down at " << tree_->FindPosition(node_)
             << " and advance " << delta << " with " << tree_->size();
  }
  size_t current_position = tree_->FindPosition(node_);
  DCHECK_EQ(original_position + original_delta, current_position);
  DVLOG(5) << "After advance: " << tree_->FindPosition(node_);
  return *this;
}

template <typename Item, bool IsConst>
TreeIterator<Item, IsConst>& TreeIterator<Item, IsConst>::operator-=(
    const int& delta) {
  return this->operator+=(-delta);
}

template <typename Item, bool IsConst>
TreeIterator<Item, IsConst> TreeIterator<Item, IsConst>::operator+(
    const int& delta) const {
  TreeIterator<Item, IsConst> result = *this;
  result += delta;
  return result;
}

template <typename Item, bool IsConst>
TreeIterator<Item, IsConst> TreeIterator<Item, IsConst>::operator-(
    const int& delta) const {
  return this->operator+(-delta);
}

template <typename Item, bool IsConst>
int TreeIterator<Item, IsConst>::operator-(
    const TreeIterator<Item, true>& other) const {
  return tree_->FindPosition(this->node_) - tree_->FindPosition(other.node_);
}

template <typename Item, bool IsConst>
int TreeIterator<Item, IsConst>::operator-(
    const TreeIterator<Item, false>& other) const {
  return tree_->FindPosition(this->node_) - tree_->FindPosition(other.node_);
}

template <typename Item, bool IsConst>
template <bool OtherIsConst>
bool TreeIterator<Item, IsConst>::operator<(
    const TreeIterator<Item, OtherIsConst>& other) const {
  return tree_->FindPosition(this->node_) < tree_->FindPosition(other.node_);
}

template <typename Item, bool IsConst>
template <bool OtherIsConst>
bool TreeIterator<Item, IsConst>::operator<=(
    const TreeIterator<Item, OtherIsConst>& other) const {
  return *this < other || *this == other;
}

template <typename Item, bool IsConst>
template <bool OtherIsConst>
bool TreeIterator<Item, IsConst>::operator>(
    const TreeIterator<Item, OtherIsConst>& other) const {
  return other < *this;
}

template <typename Item, bool IsConst>
template <bool OtherIsConst>
bool TreeIterator<Item, IsConst>::operator>=(
    const TreeIterator<Item, OtherIsConst>& other) const {
  return *this > other || *this == other;
}

template <typename Item, bool IsConst>
typename TreeIterator<Item, IsConst>::reference
    TreeIterator<Item, IsConst>::operator*() const {
  VLOG(5) << "Dereference object at: " << tree_->FindPosition(node_);
  CHECK(node_ != nullptr)
      << "Attempt to dereference iterator past end of tree.";
  return node_->item;
}

template <typename Item, bool IsConst>
TreeIterator<Item, IsConst>::operator TreeIterator<Item, true>() const {
  return TreeIterator<Item, true>(tree_, node_);
}

template <typename Item>
void Tree<Item>::ValidateInvariants(const Node<Item>* node) const {
#ifndef NDEBUG
  if (node == nullptr) { return; }
  if (node->parent == nullptr) {
    DCHECK_EQ(node, root_);
  }
  DCHECK_EQ(node->count, 1
                         + (node->left == nullptr ? 0 : node->left->count)
                         + (node->right == nullptr ? 0 : node->right->count));
  size_t left_height = Height(node->left);
  size_t right_height = Height(node->right);
  DCHECK_LE(std::max(right_height, left_height),
            std::min(right_height, left_height) + 1);
#ifdef REALLY_ALL
  if (node->left) {
    DCHECK_EQ(node->left->parent, node);
    ValidateInvariants(node->left);
  }
  if (node->right) {
    DCHECK_EQ(node->right->parent, node);
    ValidateInvariants(node->right);
  }
#endif
#endif
}

template <typename Item>
void Tree<Item>::ValidateInvariants() const {
#ifndef NDEBUG
  ValidateInvariants(root_);
  CHECK_EQ(root_ == nullptr, empty());
#endif
}

template <typename Item>
void Tree<Item>::clear() {
  ValidateInvariants();
  DeleteNodes(root_);
  root_ = nullptr;
  ValidateInvariants();
}

template <typename Item>
bool Tree<Item>::empty() const { return root_ == nullptr; }

template <typename Item>
typename Tree<Item>::reference Tree<Item>::at(size_t position) {
  return *const_cast<Item*>(
      &const_cast<const Tree<Item>*>(this)->at(position));
}

template <typename Item>
typename Tree<Item>::const_reference Tree<Item>::at(size_t position) const {
  return *(begin() + position);
}

template <typename Item>
typename Tree<Item>::iterator Tree<Item>::begin() {
  return iterator(this, FirstNode(root_));
}

template <typename Item>
typename Tree<Item>::const_iterator Tree<Item>::cbegin() {
  return const_iterator(this, FirstNode(root_));
}

template <typename Item>
typename Tree<Item>::const_iterator Tree<Item>::begin() const {
  return const_iterator(this, FirstNode(root_));
}

template <typename Item>
typename Tree<Item>::reverse_iterator Tree<Item>::rbegin() {
  return reverse_iterator(end());
}

template <typename Item>
typename Tree<Item>::const_reverse_iterator Tree<Item>::rbegin() const {
  return const_reverse_iterator(end());
}

template <typename Item>
typename Tree<Item>::iterator Tree<Item>::end() {
  ValidateInvariants();
  return iterator(this, nullptr);
}

template <typename Item>
typename Tree<Item>::const_iterator Tree<Item>::cend() {
  ValidateInvariants();
  return const_iterator(this, nullptr);
}

template <typename Item>
typename Tree<Item>::const_iterator Tree<Item>::end() const {
  ValidateInvariants();
  return const_iterator(this, nullptr);
}

template <typename Item>
size_t Tree<Item>::size() const {
  return root_ == nullptr ? 0 : root_->count;
}

template <typename Item>
void Tree<Item>::push_back(const Item& item) {
  DVLOG(6) << "Calling: push_back.";
  ValidateInvariants();
  insert(end(), item);
}

template <typename Item>
template <class... Args>
void Tree<Item>::emplace_back(Args&&... args) {
  // TODO: Give a better implementation, this one is kinda lame.
  push_back(Item(args...));
}

template <typename Item>
Item& Tree<Item>::back() {
  DCHECK(root_ != nullptr) << "Tree::back called in empty Tree.";
  ValidateInvariants();
  return *--end();
}

template <typename Item>
Item& Tree<Item>::front() {
  DCHECK(root_ != nullptr) << "Tree::front called in empty Tree.";
  ValidateInvariants();
  return *begin();
}

template <typename Item>
void Tree<Item>::insert(const iterator& position, Item item) {
  DVLOG(6) << "Calling: insert, building node.";
  insert(position, new Node<Item>(std::move(item)));
}

template <typename Item>
template <typename InputIterator>
void Tree<Item>::insert(const iterator& position, InputIterator first,
                        InputIterator last) {
  while (first != last) {
    insert(position, *first);
    ++first;
  }
}

template <typename Item>
size_t Tree<Item>::Height(const Node<Item>* node) {
  return node == nullptr ? 0 : node->height;
}

template <typename Item>
void Tree<Item>::RecomputeCounters(Node<Item>* node) {
  DVLOG(8) << "Recomputing counters for: " << node;
  DCHECK(node != nullptr);
  node->count = 1
      + (node->left == nullptr ? 0 : node->left->count)
      + (node->right == nullptr ? 0 : node->right->count);
  node->height = std::max(Height(node->left), Height(node->right)) + 1;
}

// We give this description with the names of a left rotation. For a right
// rotation, Left and Right are reversed.
template <typename Item>
template <Node<Item>* Node<Item>::*Left, Node<Item>* Node<Item>::*Right>
void Tree<Item>::Rotate(Node<Item>* node) {
  DVLOG(7) << "Rotate at " << *node << " starts as " << *root_;

  auto new_parent = node->*Right;
  auto moving_son = new_parent->*Left;

  DCHECK_EQ(new_parent->parent, node);

  new_parent->*Left = node;
  new_parent->parent = node->parent;
  if (node->parent == nullptr) {
    root_ = new_parent;
  } else if (node->parent->*Left == node) {
    node->parent->*Left = new_parent;
  } else {
    DCHECK_EQ(node->parent->*Right, node);
    node->parent->*Right = new_parent;
  }

  node->*Right = moving_son;
  node->parent = new_parent;

  if (moving_son != nullptr) {
    DCHECK_EQ(moving_son->parent, new_parent);
    moving_son->parent = node;
  }

  DVLOG(7) << "Rotate end: " << *root_;

  RecomputeCounters(node);
  RecomputeCounters(new_parent);
}

template <typename Item>
size_t Height(Node<Item>* node) {
  return node == nullptr ? 0 : node->height;
}

template <typename Item>
template <Node<Item>* Node<Item>::*Left, Node<Item>* Node<Item>::*Right>
bool Tree<Item>::MaybeRotateLeft(Node<Item>* node, bool insert) {
  DVLOG(6) << "MaybeRotate at node: " << *node;
  DCHECK(node != nullptr);
  DCHECK_GE(Height(node->*Right), Height(node->*Left));
  if (Height(node->*Right) > Height(node->*Left) + 1) {
    Node<Item>* right = node->*Right;
    bool finish = insert;
    if (Height(right->*Left) > Height(right->*Right)) {
      DVLOG(7) << "Rotate right.";
      Rotate<Right, Left>(right);
    } else if (Height(right->*Left) == Height(right->*Right)) {
      finish = true;
    }
    DVLOG(7) << "Rotate left.";
    Rotate<Left, Right>(node);
    return finish;
  }
  // No need to keep going if the insertion rebalanced the tree.
  return insert && Height(node->*Left) == Height(node->*Right);
}

template <typename Item>
void Tree<Item>::MaybeRebalance(Node<Item>* node, const Node<Item>* stop,
                                bool insert) {
  if (node == nullptr) { return; }
  ValidateInvariants(node->left);
  ValidateInvariants(node->right);
  DVLOG(5) << "Maybe rebalance in tree " << *this << " at node " << *node;
  while (node != stop) {
    auto parent = node->parent;
    DCHECK(node->parent == nullptr
           || node == parent->left
           || node == parent->right);
    DVLOG(9) << "Starting maybe rebalance iteration at " << *node;
    RecomputeCounters(node);
    if (Height(node->right) > Height(node->left)) {
      DVLOG(5) << "Maybe rotate.";
      if (MaybeRotateLeft<&Node<Item>::left, &Node<Item>::right>(
              node, insert)) {
        VLOG(6) << "Stop";
        break;
      }
    } else if (Height(node->left) > Height(node->right)) {
      DVLOG(5) << "Maybe rotate flipped.";
      if (MaybeRotateLeft<&Node<Item>::right, &Node<Item>::left>(
              node, insert)) {
        VLOG(6) << "Stop (flipped)";
        break;
      }
    } else if (insert) {
      break;
    }
    ValidateInvariants(node);
    node = parent;
  }
  DVLOG(5) << "Done with balance loop.";
  while (node != stop) {
    RecomputeCounters(node);
    ValidateInvariants(node);
    node = node->parent;
  }
}

template <typename Item>
void Tree<Item>::InsertRight(Node<Item>* parent, Node<Item>* node) {
  DCHECK(parent->right == nullptr);
  DVLOG(5) << "Insert right: " << node << " under " << parent;
  node->parent = parent;
  parent->right = node;
  ValidateInvariants(node);
  MaybeRebalance(parent, nullptr, true);
}

template <typename Item>
void Tree<Item>::insert(const iterator& position, Node<Item>* node) {
  DVLOG(6) << "Insert with node begins: " << node << " position: "
           << position.node_;
  ValidateInvariants();

  if (position.node_ == nullptr) {  // Inserting after all elements.
    if (root_ == nullptr) {
      root_ = node;
    } else {
      Node<Item>* parent = (--end()).node_;
      DVLOG(8) << "Parent: " << parent;
      CHECK(parent != nullptr);
      InsertRight(parent, node);
    }
  } else {
    auto parent = position.node_;
    if (!parent->left) {
      DVLOG(5) << "Insert left: " << node << " under " << parent;
      parent->left = node;
      node->parent = parent;
      ValidateInvariants(node);
      MaybeRebalance(node->parent, nullptr, true);
    } else {
      parent = parent->left;
      while (parent->right != nullptr) {
        parent = parent->right;
      }
      InsertRight(parent, node);
    }
  }
  ValidateInvariants();
}

template <typename Item>
void Tree<Item>::ReplaceNode(const Node<Item>* old_node, Node<Item>* new_node) {
  DCHECK(old_node != nullptr);
  DCHECK(old_node != new_node);
  Node<Item>* parent = old_node->parent;
  if (parent == nullptr) {
    DCHECK(root_ == old_node);
    root_ = new_node;
  } else if (parent->left == old_node) {
    parent->left = new_node;
  } else {
    DCHECK(parent->right == old_node);
    parent->right = new_node;
  }
  if (new_node) {
    new_node->parent = parent;
  }
}

template <typename Item>
typename Tree<Item>::iterator Tree<Item>::erase(iterator position) {
  ValidateInvariants();

  // The node to remove.
  const Node<Item>* node = position.node_;
  DCHECK(node != nullptr) << "Attempt to erase from tree past the end.";

  // The node that will replace the old node.
  Node<Item>* next_node;

  if (node->right == nullptr) {
    next_node = node->left;
    ReplaceNode(node, next_node);
    MaybeRebalance(node->parent, nullptr, false);
  } else {
    // Find the first element after node.
    next_node = node->right;
    while (next_node->left != nullptr) {
      next_node = next_node->left;
    }
    ReplaceNode(next_node, next_node->right);
    MaybeRebalance(next_node->parent, node, false);

    next_node->left = node->left;
    if (next_node->left) { next_node->left->parent = next_node; }
    next_node->right = node->right;
    if (next_node->right) { next_node->right->parent = next_node; }

    DCHECK(node != nullptr);
    ReplaceNode(node, next_node);
    MaybeRebalance(next_node, nullptr, false);
  }

  delete node;
  ValidateInvariants();
  return iterator(this, next_node);
}

template <typename Item>
typename Tree<Item>::iterator Tree<Item>::erase(iterator start, iterator end) {
  // TODO: A much more efficient implementation is possible. This is linear to
  // end - start. We could do logn.
  while (start != end) {
    CHECK(start < end);
    auto next = start + 1;
    erase(start);
    start = next;
  }
  return end;
}

template <typename Item>
Node<Item>* Tree<Item>::FirstNode(Node<Item>* node) {
  return const_cast<Node<Item>*>(
      const_cast<const Tree<Item>*>(this)->FirstNode(node));
}

template <typename Item>
const Node<Item>* Tree<Item>::FirstNode(const Node<Item>* node) const {
  ValidateInvariants();
  if (node != nullptr) {
    while (node->left != nullptr) {
      node = node->left;
    }
  }
  return node;
}

template <typename Item>
Node<Item>* Tree<Item>::LastNode(Node<Item>* node) {
  return const_cast<Node<Item>*>(
      const_cast<const Tree<Item>*>(this)->LastNode(node));
}

template <typename Item>
const Node<Item>* Tree<Item>::LastNode(const Node<Item>* node) const {
  ValidateInvariants();
  if (node != nullptr) {
    while (node->right != nullptr) {
      node = node->right;
    }
  }
  return node;
}

template <typename Item>
void Tree<Item>::DeleteNodes(Node<Item>* node) {
  if (node == nullptr) { return; }
  DeleteNodes(node->left);
  DeleteNodes(node->right);
  delete node;
}

template <typename Item>
size_t Tree<Item>::FindPosition(const Node<Item>* node) const {
  if (node == nullptr) {
    return root_ == nullptr ? 0 : root_->count;
  }
  int count = Count(node->left);
  while (node->parent != nullptr) {
    if (node->parent->right == node) {
      count += 1 + Count(node->parent->left);
    }
    node = node->parent;
  }
  return count;
}

}  // namespace editor
}  // namespace afc

#endif  // __AFC_EDITOR_TREE_H__
