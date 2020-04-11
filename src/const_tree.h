#ifndef __AFC_EDITOR_CONST_TREE_H__
#define __AFC_EDITOR_CONST_TREE_H__

#include <glog/logging.h>

#include <memory>

namespace afc::editor {

// An immutable tree supporting fast `Prefix` (get initial sequence), `Suffix`,
// and `Append` operations.
template <typename T>
class ConstTree {
 private:
  struct ConstructorAccessTag {
   private:
    ConstructorAccessTag() = default;
    friend ConstTree<T>;
  };

 public:
  using Ptr = std::shared_ptr<ConstTree<T>>;

  // Only `New` should be calling this.
  ConstTree(ConstructorAccessTag, T element, Ptr left, Ptr right)
      : depth_(1 + std::max(Depth(left), Depth(right))),
        size_(1 + Size(left) + Size(right)),
        element_(std::move(element)),
        left_(std::move(left)),
        right_(std::move(right)) {
    CHECK_LE(std::max(Depth(left), Depth(right)),
             std::min(Depth(left), Depth(right)) + 1);
  }

  static Ptr Leaf(T element) {
    return New(std::move(element), nullptr, nullptr);
  }

  static Ptr Append(const Ptr& a, const Ptr& b) {
    if (a == nullptr) return b;
    if (b == nullptr) return a;
    return New(a->Last(), a->MinusLast(), b);
  }

  static Ptr PushBack(const Ptr& a, T element) {
    return New(std::move(element), a, nullptr);
  }

  static Ptr Insert(const Ptr& tree, size_t index, T element) {
    CHECK_LE(index, Size(tree));
    if (tree == nullptr) return Leaf(std::move(element));
    auto size_left = Size(tree->left_);
    if (index <= size_left)
      return New(tree->element_, Insert(tree->left_, index, std::move(element)),
                 tree->right_);
    else
      return New(
          tree->element_, tree->left_,
          Insert(tree->right_, index - size_left - 1, std::move(element)));
  }

  static Ptr Erase(const Ptr& tree, size_t index) {
    CHECK_LE(index, Size(tree));
    auto size_left = Size(tree->left_);
    if (index < size_left) {
      return New(tree->element_, Erase(tree->left_, index), tree->right_);
    } else if (index > size_left) {
      return New(tree->element_, tree->left_,
                 Erase(tree->right_, index - size_left - 1));
    } else if (tree->left_ == nullptr) {
      return tree->right_;
    } else {
      return New(tree->left_->element_,
                 Erase(tree->left_, Size(tree->left_->left_)), tree->right_);
    }
  }

  Ptr Replace(size_t index, T new_element) {
    auto size_left = Size(left_);
    if (index < size_left) {
      return NewFinal(element_, left_->Replace(index, std::move(new_element)),
                      right_);
    } else if (index > size_left) {
      return NewFinal(
          element_, left_,
          right_->Replace(index - size_left - 1, std::move(new_element)));
    } else {
      return NewFinal(new_element, left_, right_);
    }
  }
  static size_t Size(const Ptr& tree) {
    return tree == nullptr ? 0 : tree->size_;
  }

  static size_t Depth(const Ptr& tree) {
    return tree == nullptr ? 0 : tree->depth_;
  }

  const T& Get(size_t i) {
    CHECK_LT(i, size_);
    auto size_left = Size(left_);
    if (i < size_left) {
      CHECK(left_ != nullptr);
      return left_->Get(i);
    } else if (i == size_left) {
      return element_;
    } else {
      CHECK(right_ != nullptr);
      return right_->Get(i - size_left - 1);
    }
  }

  // Returns a tree containing the first len elements. Prefix("abcde", 2) ==
  // "ab".
  static Ptr Prefix(const Ptr& a, size_t len) {
    if (len == Size(a)) return a;
    CHECK(a != nullptr);
    CHECK_LT(len, a->size_);
    auto size_left = Size(a->left_);
    if (len <= size_left) {
      return Prefix(a->left_, len);
    }
    return New(a->element_, a->left_, Prefix(a->right_, len - size_left - 1));
  }

  // Returns a tree skipping the first len elements (i.e., from element `len` to
  // the end).
  static Ptr Suffix(const Ptr& a, size_t len) {
    if (len >= Size(a)) return nullptr;
    CHECK(a != nullptr);
    CHECK_LT(len, a->size_);
    auto size_left = Size(a->left_);
    if (len >= size_left + 1) {
      return Suffix(a->right_, len - size_left - 1);
    }
    return New(a->element_, Suffix(a->left_, len), a->right_);
  }

  // Similar to std::upper_bound(begin(), end(), val, compare). Returns the
  // index of the first element greater than key. The elements in the tree must
  // be sorted (according to the less_than value given).
  template <typename LessThan>
  static size_t UpperBound(const Ptr& tree, const T& key, LessThan less_than) {
    if (tree == nullptr) {
      return 0;
    } else if (less_than(key, tree->element_)) {
      return UpperBound(tree->left_, key, less_than);
    } else {
      return Size(tree->left_) + 1 + UpperBound(tree->right_, key, less_than);
    }
  }

  template <typename Predicate>
  static bool Every(const Ptr& tree, Predicate predicate) {
    return tree == nullptr ||
           (Every(tree->left_, predicate) && predicate(tree->element_) &&
            Every(tree->right_, predicate));
  }

 private:
  T Last() { return right_ == nullptr ? element_ : right_->Last(); }

  Ptr MinusLast() {
    return right_ == nullptr ? left_
                             : New(element_, left_, right_->MinusLast());
  }

  Ptr RotateRight() {
    CHECK(left_ != nullptr);
    return NewFinal(left_->element_, left_->left_,
                    NewFinal(element_, left_->right_, right_));
  }

  Ptr RotateLeft() {
    CHECK(right_ != nullptr);
    return NewFinal(right_->element_, NewFinal(element_, left_, right_->left_),
                    right_->right_);
  }

  static Ptr New(T element, Ptr left, Ptr right) {
    VLOG(5) << "New with depths: " << Depth(left) << ", " << Depth(right);
    if (Depth(right) > Depth(left) + 1) {
      if (Depth(right->left_) > Depth(right->right_)) {
        right = right->RotateRight();
      }
      return NewFinal(right->element_, New(element, left, right->left_),
                      right->right_);
    } else if (Depth(left) > Depth(right) + 1) {
      if (Depth(left->right_) > Depth(left->left_)) {
        left = left->RotateLeft();
      }
      return NewFinal(left->element_, left->left_,
                      New(element, left->right_, right));
    }
    return NewFinal(element, left, right);
  }

  static Ptr NewFinal(T element, Ptr left, Ptr right) {
    return std::make_shared<ConstTree<T>>(ConstructorAccessTag{}, element, left,
                                          right);
  }

  const size_t depth_;
  const size_t size_;
  const T element_;

  const std::shared_ptr<ConstTree<T>> left_;
  const std::shared_ptr<ConstTree<T>> right_;
};

}  // namespace afc::editor

#endif  //  __AFC_EDITOR_CONST_TREE_H__
