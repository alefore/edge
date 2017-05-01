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

// A sequence of elements, stored internally as a tree (to provide quick
// insertion/deletion).
//
// The interface provided is *not* a tree: it's a sequence of elements.
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

  Tree() = default;
  Tree(const Tree& tree);
  ~Tree() = default;
  Tree<Item>& operator=(const Tree<Item>& tree);

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
  const_iterator cbegin() const;
  const_iterator begin() const;
  reverse_iterator rbegin();
  const_reverse_iterator rbegin() const;

  // Returns an iterator pointing to the end of the Tree (one after the last
  // element).
  iterator end();
  const_iterator cend() const;
  const_iterator end() const;
  reverse_iterator rend();
  const_reverse_iterator rend() const;

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

  // Similar to std::upper_bound(begin(), end(), val, compare), but drastically
  // more efficient. Requires that the elements in the tree are sorted
  // (according to the compare value given).
  template <typename T, typename Compare>
  iterator UpperBound(const T& val, Compare compare);
  template <typename T, typename Compare>
  const_iterator UpperBound(const T& val, Compare compare) const;


 private:
  static size_t Count(Node<Item>* node);
  static size_t Height(const Node<Item>* node);

  void ValidateInvariants() const;
  void ValidateInvariants(const Node<Item>* node) const;

  void RecomputeCounters(Node<Item>* node);

  // Rotates the tree to the right or to the left.
  template <std::unique_ptr<Node<Item>> Node<Item>::*Left,
            std::unique_ptr<Node<Item>> Node<Item>::* Right>
  void Rotate(Node<Item>* node);
  // Takes old out of a tree, replacing it with new.
  std::unique_ptr<Node<Item>> ReplaceNode(const Node<Item>* old_node,
                                          std::unique_ptr<Node<Item>> new_node);

  template <std::unique_ptr<Node<Item>> Node<Item>::*Left,
            std::unique_ptr<Node<Item>> Node<Item>::*Right>
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
  void InsertRight(Node<Item>* parent, std::unique_ptr<Node<Item>> node);

  // Inserts an element at level 0 at the position specified. node is the new
  // node to insert.
  void insert(const iterator& position, std::unique_ptr<Node<Item>> node);

  // Returns the left-most element under node. If node is nullptr, just returns
  // it.
  const Node<Item>* FirstNode(const Node<Item>* node) const;
  Node<Item>* FirstNode(Node<Item>* node);
  const Node<Item>* LastNode(const Node<Item>* node) const;
  Node<Item>* LastNode(Node<Item>* node);

  size_t FindPosition(const Node<Item>* node) const;

  friend class TreeIterator<Item, false>;
  friend class TreeIterator<Item, true>;
  friend std::ostream& operator<< <>(std::ostream& out, const Tree<Item>& tree);
  friend std::ostream& operator<< <>(std::ostream& out, const Node<Item>& tree);

  std::unique_ptr<Node<Item>> root_;
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
  std::unique_ptr<Node<Item>> left;
  std::unique_ptr<Node<Item>> right;

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
    node_ = tree_->LastNode(tree_->root_.get());
    if (node_ != nullptr) { delta++; }
    DVLOG(7) << "Adjusted: " << tree_->FindPosition(node_) << " and advance "
             << delta << " with " << tree_->size();
  } else {
    DVLOG(6) << "Now at: " << *node_;
  }

  // Go up one level in each iteration until we know we can go down.
  while (node_ != nullptr
         && ((delta > 0)
                  ? delta > Tree<Item>::Count(node_->right.get())
                  : -delta > Tree<Item>::Count(node_->left.get()))) {
    if (node_->parent == nullptr || node_->parent->left.get() == node_) {
      DVLOG(7) << "Going up through left branch";
      delta -= 1 + Tree<Item>::Count(node_->right.get());
    } else {
      DVLOG(7) << "Going up through right branch";
      delta += 1 + Tree<Item>::Count(node_->left.get());
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
      node_ = node_->right.get();
      delta = delta - 1 - Tree<Item>::Count(node_->left.get());
    } else {
      DVLOG(6) << "Down the left branch.";
      node_ = node_->left.get();
      delta = Tree<Item>::Count(node_->right.get()) + delta + 1;
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
    DCHECK_EQ(node, root_.get());
  }
  DCHECK_EQ(node->count, 1
                         + (node->left == nullptr ? 0 : node->left->count)
                         + (node->right == nullptr ? 0 : node->right->count));
  size_t left_height = Height(node->left.get());
  size_t right_height = Height(node->right.get());
  DCHECK_LE(std::max(right_height, left_height),
            std::min(right_height, left_height) + 1);
#ifdef REALLY_ALL
  if (node->left) {
    DCHECK_EQ(node->left->parent, node);
    ValidateInvariants(node->left.get());
  }
  if (node->right) {
    DCHECK_EQ(node->right->parent, node);
    ValidateInvariants(node->right.get());
  }
#endif
#endif
}

template <typename Item>
void Tree<Item>::ValidateInvariants() const {
#ifndef NDEBUG
  ValidateInvariants(root_.get());
  CHECK_EQ(root_ == nullptr, empty());
#endif
}

template <typename Item>
Tree<Item>::Tree(const Tree& tree) : Tree() {
  *this = tree;
}

template <typename Item>
Tree<Item>& Tree<Item>::operator=(const Tree& tree) {
  clear();
  insert(begin(), tree.cbegin(), tree.cend());
  return *this;
}

template <typename Item>
void Tree<Item>::clear() {
  ValidateInvariants();
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
  return iterator(this, FirstNode(root_.get()));
}

template <typename Item>
typename Tree<Item>::const_iterator Tree<Item>::cbegin() const {
  return const_iterator(this, FirstNode(root_.get()));
}

template <typename Item>
typename Tree<Item>::const_iterator Tree<Item>::begin() const {
  return const_iterator(this, FirstNode(root_.get()));
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
typename Tree<Item>::const_iterator Tree<Item>::cend() const {
  ValidateInvariants();
  return const_iterator(this, nullptr);
}

template <typename Item>
typename Tree<Item>::const_iterator Tree<Item>::end() const {
  ValidateInvariants();
  return const_iterator(this, nullptr);
}

template <typename Item>
typename Tree<Item>::reverse_iterator Tree<Item>::rend() {
  return reverse_iterator(begin());
}

template <typename Item>
typename Tree<Item>::const_reverse_iterator Tree<Item>::rend() const {
  return const_reverse_iterator(cbegin());
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
  insert(position,
         std::unique_ptr<Node<Item>>(new Node<Item>(std::move(item))));
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
  node->height =
      std::max(Height(node->left.get()), Height(node->right.get())) + 1;
}

// We give this description with the names of a left rotation. For a right
// rotation, Left and Right are reversed.
//
// Goes from:
// [D [B A C] [F E G]]
// To:
// [F [D [B A C] E] G]
template <typename Item>
template <std::unique_ptr<Node<Item>> Node<Item>::*Left,
          std::unique_ptr<Node<Item>> Node<Item>::*Right>
void Tree<Item>::Rotate(Node<Item>* node) {
  DVLOG(7) << "Rotate at " << *node << " starts as " << *root_;

  std::unique_ptr<Node<Item>>* node_ptr;
  if (node->parent == nullptr) {
    node_ptr = &root_;
  } else if ((node->parent->*Left).get() == node) {
    node_ptr = &(node->parent->*Left);
  } else {
    node_ptr = &(node->parent->*Right);
  }
  DCHECK_EQ(node_ptr->get(), node);
  // node: [D [B A C] [F E G]]

  std::unique_ptr<Node<Item>> new_parent = std::move(node->*Right);
  new_parent->parent = node->parent;
  // node: [D [B A C] -]
  // new_parent: [F E G]

  node->*Right = std::move(new_parent.get()->*Left);
  if (node->*Right != nullptr) {
    DCHECK_EQ((node->*Right)->parent, new_parent.get());
    (node->*Right)->parent = node;
  }
  // node: [D [B A C] E]
  // new_parent: [F - G]

  new_parent.get()->*Left = std::move(*node_ptr);
  node->parent = new_parent.get();
  // node: -
  // new_parent: [F [D [B A C] E] G]

  // Now install new_parent where node was.
  *node_ptr = std::move(new_parent);

  DVLOG(7) << "Rotate end: " << *root_;

  RecomputeCounters(node);
  RecomputeCounters(node_ptr->get());
}

template <typename Item>
size_t Height(Node<Item>* node) {
  return node == nullptr ? 0 : node->height;
}

template <typename Item>
template <std::unique_ptr<Node<Item>> Node<Item>::*Left,
          std::unique_ptr<Node<Item>> Node<Item>::*Right>
bool Tree<Item>::MaybeRotateLeft(Node<Item>* node, bool insert) {
  DVLOG(6) << "MaybeRotate at node: " << *node;
  DCHECK(node != nullptr);
  DCHECK_GE(Height((node->*Right).get()), Height((node->*Left).get()));
  if (Height((node->*Right).get()) > Height((node->*Left).get()) + 1) {
    Node<Item>* right = (node->*Right).get();
    bool finish = insert;
    if (Height((right->*Left).get()) > Height((right->*Right).get())) {
      DVLOG(7) << "Rotate right.";
      Rotate<Right, Left>(right);
    } else if (Height((right->*Left).get()) == Height((right->*Right).get())) {
      finish = true;
    }
    DVLOG(7) << "Rotate left.";
    Rotate<Left, Right>(node);
    return finish;
  }
  // No need to keep going if the insertion rebalanced the tree.
  return insert && Height((node->*Left).get()) == Height((node->*Right).get());
}

template <typename Item>
void Tree<Item>::MaybeRebalance(Node<Item>* node, const Node<Item>* stop,
                                bool insert) {
  if (node == nullptr) { return; }
  ValidateInvariants(node->left.get());
  ValidateInvariants(node->right.get());
  DVLOG(5) << "Maybe rebalance in tree " << *this << " at node " << *node;
  while (node != stop) {
    auto parent = node->parent;
    DCHECK(node->parent == nullptr
           || node == parent->left.get()
           || node == parent->right.get());
    DVLOG(9) << "Starting maybe rebalance iteration at " << *node;
    RecomputeCounters(node);
    if (Height(node->right.get()) > Height(node->left.get())) {
      DVLOG(5) << "Maybe rotate.";
      if (MaybeRotateLeft<&Node<Item>::left, &Node<Item>::right>(
              node, insert)) {
        VLOG(6) << "Stop";
        break;
      }
    } else if (Height(node->left.get()) > Height(node->right.get())) {
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
void Tree<Item>::InsertRight(Node<Item>* parent,
                             std::unique_ptr<Node<Item>> node) {
  DCHECK(parent->right == nullptr);
  DVLOG(5) << "Insert right: " << node.get() << " under " << parent;
  node->parent = parent;
  parent->right = std::move(node);
  ValidateInvariants(parent->right.get());
  MaybeRebalance(parent, nullptr, true);
}

template <typename Item>
void Tree<Item>::insert(const iterator& position,
                        std::unique_ptr<Node<Item>> node) {
  DVLOG(6) << "Insert with node begins: " << node.get() << " position: "
           << position.node_;
  ValidateInvariants();

  if (position.node_ == nullptr) {  // Inserting after all elements.
    if (root_ == nullptr) {
      root_ = std::move(node);
    } else {
      Node<Item>* parent = (--end()).node_;
      DVLOG(8) << "Parent: " << parent;
      CHECK(parent != nullptr);
      InsertRight(parent, std::move(node));
    }
  } else {
    auto parent = position.node_;
    if (!parent->left) {
      DVLOG(5) << "Insert left: " << node.get() << " under " << parent;
      node->parent = parent;
      parent->left = std::move(node);
      ValidateInvariants(parent->left.get());
      MaybeRebalance(parent, nullptr, true);
    } else {
      parent = parent->left.get();
      while (parent->right != nullptr) {
        parent = parent->right.get();
      }
      InsertRight(parent, std::move(node));
    }
  }
  ValidateInvariants();
}

template <typename Item>
std::unique_ptr<Node<Item>>
Tree<Item>::ReplaceNode(const Node<Item>* old_node,
                        std::unique_ptr<Node<Item>> new_node) {
  DCHECK(old_node != nullptr);
  DCHECK(old_node != new_node.get());
  if (new_node != nullptr) {
    new_node->parent = old_node->parent;
  }
  std::unique_ptr<Node<Item>>* source;
  if (old_node->parent == nullptr) {
    source = &root_;
  } else if (old_node->parent->left.get() == old_node) {
    source = &old_node->parent->left;
  } else {
    source = &old_node->parent->right;
  }
  DCHECK(source->get() == old_node);
  new_node.swap(*source);
  return std::move(new_node);
}

template <typename Item>
typename Tree<Item>::iterator Tree<Item>::erase(iterator position) {
  ValidateInvariants();

  // The node to remove.
  Node<Item>* node = position.node_;
  DCHECK(node != nullptr) << "Attempt to erase from tree past the end.";

  // The node that will replace the old node.
  Node<Item>* next_node;

  if (node->right == nullptr) {
    next_node = node->left.get();
    auto old_node = ReplaceNode(node, std::move(node->left));
    MaybeRebalance(old_node->parent, nullptr, false);
  } else {
    // Find the first element after node.
    next_node = node->right.get();
    while (next_node->left != nullptr) {
      next_node = next_node->left.get();
    }
    auto tmp = next_node->right.get();
    auto old_node = ReplaceNode(next_node, std::move(next_node->right));
    next_node = tmp;
    MaybeRebalance(old_node->parent, node, false);

    old_node->left = std::move(node->left);
    if (old_node->left) {
      old_node->left->parent = old_node.get();
    }
    old_node->right = std::move(node->right);
    if (old_node->right) {
      old_node->right->parent = old_node.get();
    }

    DCHECK(node != nullptr);
    tmp = old_node.get();
    ReplaceNode(node, std::move(old_node));
    MaybeRebalance(tmp, nullptr, false);
  }

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
template <typename T, typename Compare>
typename Tree<Item>::iterator Tree<Item>::UpperBound(
    const T& val, Compare compare) {
  return iterator(this, const_cast<Node<Item>*>(
      const_cast<const Tree<Item>*>(this)->UpperBound(val, compare)->node_));
}

template <typename Item>
template <typename T, typename Compare>
typename Tree<Item>::const_iterator Tree<Item>::UpperBound(
    const T& val, Compare compare) const {
  const Node<Item>* node = root_.get();
  const Node<Item>* smallest_bound_found = nullptr;
  while (node != nullptr) {
    if (!compare(val, node->item)) {
      // Recurse to the right if we're at a smaller or equal element.
      node = node->right.get();
    } else {
      // We're at a larger node. Find the upper bound at the left node.
      smallest_bound_found = node;
      node = node->left.get();
    }
  }
  return const_iterator(this, smallest_bound_found);
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
      node = node->left.get();
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
      node = node->right.get();
    }
  }
  return node;
}

template <typename Item>
size_t Tree<Item>::FindPosition(const Node<Item>* node) const {
  if (node == nullptr) {
    return root_ == nullptr ? 0 : root_->count;
  }
  int count = Count(node->left.get());
  while (node->parent != nullptr) {
    if (node->parent->right.get() == node) {
      count += 1 + Count(node->parent->left.get());
    }
    node = node->parent;
  }
  return count;
}

}  // namespace editor
}  // namespace afc

#endif  // __AFC_EDITOR_TREE_H__
