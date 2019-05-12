#ifndef __AFC_EDITOR_CONST_TREE_H__
#define __AFC_EDITOR_CONST_TREE_H__

#include <glog/logging.h>

#include <memory>

namespace afc {
namespace editor {

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
        right_(std::move(right)) {}

  static Ptr Append(const Ptr& a, const Ptr& b) {
    if (a == nullptr) return b;
    return New(a->element_, a->left_, Append(a->right_, std::move(b)));
  }

  static Ptr PushBack(const Ptr& a, T element) {
    return Append(a, New(std::move(element), nullptr, nullptr));
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

  // Returns a tree containing the first len elements.
  static Ptr Prefix(const Ptr& a, size_t len) {
    if (len == 0) return nullptr;
    CHECK(a != nullptr);
    CHECK_LE(len, a->size_);
    auto size_left = Size(a->left_);
    if (len <= size_left) {
      CHECK(a->left_ != nullptr);
      return Prefix(a->left_, len);
    }
    auto prefix = PushBack(a->left_, a->element_);
    return Append(prefix, Prefix(a->right, len - prefix->size()));
  }

 private:
  static Ptr RotateRight(Ptr tree) {
    CHECK(tree != nullptr);
    CHECK(tree->left_ != nullptr);
    return std::make_shared<ConstTree<T>>(
        ConstructorAccessTag(), tree->left_->element_, tree->left_->left_,
        std::make_shared<ConstTree<T>>(ConstructorAccessTag(), tree->element_,
                                       tree->left_->right_, tree->right_));
  }

  static Ptr RotateLeft(Ptr tree) {
    CHECK(tree != nullptr);
    CHECK(tree->right_ != nullptr);
    return std::make_shared<ConstTree<T>>(
        ConstructorAccessTag(), tree->right_->element_,
        std::make_shared<ConstTree<T>>(ConstructorAccessTag(), tree->element_,
                                       tree->left_, tree->right_->left_),
        tree->right_->right_);
  }

  static Ptr New(T element, Ptr left, Ptr right) {
    VLOG(5) << "New with depths: " << Depth(left) << ", " << Depth(right);
    if (Depth(right) > Depth(left) + 1) {
      CHECK(right != nullptr);
      if (Depth(right->left_) > Depth(right->right_)) {
        right = RotateRight(std::move(right));
      }
      return New(right->element_, New(element, left, right->left_),
                 right->right_);
    } else if (Depth(left) > Depth(right) + 1) {
      CHECK(left != nullptr);
      if (Depth(left->right_) > Depth(left->left_)) {
        left = RotateLeft(std::move(left));
      }
      return New(left->element_, left->left_,
                 New(element, left->right_, right));
    }
    return std::make_shared<ConstTree<T>>(ConstructorAccessTag{}, element, left,
                                          right);
  }

  const int depth_;
  const int size_;
  const T element_;

  const std::shared_ptr<ConstTree<T>> left_;
  const std::shared_ptr<ConstTree<T>> right_;
};  // namespace editor

}  // namespace editor
}  // namespace afc

#endif  //  __AFC_EDITOR_CONST_TREE_H__
