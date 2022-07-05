#ifndef __AFC_LANGUAGE_CONST_TREE_H__
#define __AFC_LANGUAGE_CONST_TREE_H__

#include <glog/logging.h>

#include <memory>
#include <optional>
#include <variant>

namespace afc::language {

// Captures a pointer to an object that we may uniquely own (and thus it's safe
// to modify) or may share with other owners (and thus retain only const
// access).
template <typename T>
using PtrVariant = std::variant<std::unique_ptr<T>, std::shared_ptr<const T>>;

template <typename T>
inline PtrVariant<T> MakePtrVariant(T value) {
  return std::make_unique<T>(std::move(value));
}

template <typename T>
const T* AddressOf(const PtrVariant<T>& p) {
  if (auto u = std::get_if<std::unique_ptr<T>>(&p); u != nullptr) {
    return u->get();
  }
  return std::get<std::shared_ptr<const T>>(p).get();
}

template <typename T>
inline std::unique_ptr<T> ToUnique(PtrVariant<T> p) {
  if (auto u = std::get_if<std::unique_ptr<T>>(&p); u != nullptr) {
    return std::move(*u);
  }
  if (auto shared_const = std::get<std::shared_ptr<const T>>(p);
      shared_const != nullptr)
    return std::make_unique<T>(shared_const->Copy());
  return nullptr;
}

template <typename T>
inline T ToUniqueValue(PtrVariant<T> p) {
  return std::move(*ToUnique(std::move(p)));
}

template <typename T>
inline std::shared_ptr<const T> ToSharedConst(PtrVariant<T> p) {
  return std::visit([](auto v) -> std::shared_ptr<const T> { return v; },
                    std::move(p));
}

template <typename T, size_t ExpectedSize>
class VectorBlock {
  struct ConstructorAccessTag {};

 public:
  using ValueType = T;
  using Ptr = std::shared_ptr<const VectorBlock>;

  VectorBlock(ConstructorAccessTag, std::vector<T> v) : values_(std::move(v)) {
    values_.reserve(ExpectedSize);
  }

  VectorBlock(const VectorBlock&) = delete;
  VectorBlock(VectorBlock&&) = default;

  VectorBlock Copy() const {
    return VectorBlock(ConstructorAccessTag(), values_);
  }

  std::shared_ptr<const VectorBlock> Share() && {
    return std::make_shared<VectorBlock>(std::move(*this));
  }

  static VectorBlock Leaf(T&& value) {
    return VectorBlock(ConstructorAccessTag(),
                       std::vector<T>({std::forward<T>(value)}));
  }

  VectorBlock Insert(size_t index, T&& value) const {
    CHECK_LE(index, size());
    std::vector<T> values;
    values.insert(values.end(), values_.begin(), values_.begin() + index);
    values.insert(values.end(), std::move(value));
    values.insert(values.end(), values_.begin() + index, values_.end());
    return VectorBlock(ConstructorAccessTag(), std::move(values));
  }

  VectorBlock Erase(size_t index) const {
    CHECK_LT(index, size());
    std::vector<T> values;
    values.insert(values.end(), values_.begin(), values_.begin() + index);
    values.insert(values.end(), values_.begin() + index + 1, values_.end());
    return VectorBlock(ConstructorAccessTag(), std::move(values));
  }

  static std::pair<VectorBlock, VectorBlock> Split(VectorBlock a) {
    const size_t split_index = a.size() / 2;
    VLOG(5) << "Split block of size: " << a.size();
    std::vector<T> tail(
        std::make_move_iterator(a.values_.begin() + split_index),
        std::make_move_iterator(a.values_.end()));
    a.values_.resize(split_index);
    return {std::move(a), VectorBlock(ConstructorAccessTag(), std::move(tail))};
  }

  static VectorBlock Append(VectorBlock a, VectorBlock b) {
    std::vector<T> values = std::move(a.values_);
    values.insert(values.end(), std::make_move_iterator(b.values_.begin()),
                  std::make_move_iterator(b.values_.end()));
    return VectorBlock(ConstructorAccessTag(), std::move(values));
  }

  VectorBlock Prefix(size_t len) const {
    CHECK_GT(len, 0ul);
    CHECK_LE(len, values_.size());
    return VectorBlock(ConstructorAccessTag(),
                       std::vector<T>(values_.begin(), values_.begin() + len));
  }

  VectorBlock Suffix(size_t len) const {
    CHECK_LE(len, values_.size());
    return VectorBlock(ConstructorAccessTag(),
                       std::vector<T>(values_.begin() + len, values_.end()));
  }

  template <typename Predicate>
  static bool Every(const std::shared_ptr<const VectorBlock>& v,
                    Predicate& predicate) {
    for (const auto& c : v->values_)
      if (!predicate(c)) return false;
    return true;
  }

  size_t size() const { return values_.size(); }

  const T& Get(size_t index) const {
    CHECK_LT(index, size());
    return values_.at(index);
  }

  template <typename LessThan>
  size_t UpperBound(const ValueType& key, LessThan less_than) const {
    return std::distance(
        values_.begin(),
        std::upper_bound(values_.begin(), values_.end(), key, less_than));
  }

  VectorBlock Replace(size_t index, T new_value) const {
    CHECK_LT(index, size());
    std::vector<T> values_copy = values_;
    values_copy[index] = std::move(new_value);
    return VectorBlock(ConstructorAccessTag(), std::move(values_copy));
  }

  std::vector<T> values_;
};

// An immutable tree supporting fast `Prefix` (get initial sequence), `Suffix`,
// and `Append` operations.
template <typename Block, size_t MaxBlockSize = 256,
          bool ExpensiveValidation = false>
class ConstTree {
  struct ConstructorAccessTag {};

 public:
  using ValueType = Block::ValueType;
  using Ptr = std::shared_ptr<const ConstTree>;

  ConstTree(ConstructorAccessTag, std::shared_ptr<const Block> block,
            PtrVariant<ConstTree> left, PtrVariant<ConstTree> right)
      : block_(std::move(block)),
        left_(ToSharedConst(std::move(left))),
        right_(ToSharedConst(std::move(right))),
        depth_(1 + std::max(Depth(left_), Depth(right_))),
        size_(block_->size() + Size(left_) + Size(right_)) {
#if 0
    CHECK_LE(std::max(Depth(left_), Depth(right_)),
             std::min(Depth(left_), Depth(right_)) + 1)
        << "Imbalanced tree: " << Depth(left_) << " vs " << Depth(right_);
#endif
    CHECK_GE(block_->size(), 1ul);
    CHECK_LE(block_->size(), MaxBlockSize);
    ValidateHalfFullInvariant(this, true);
  }

  ConstTree Copy() const {
    return ConstTree(ConstructorAccessTag(), block_, left_, right_);
  }

  Ptr Share() && { return std::make_shared<ConstTree>(std::move(*this)); }

  static ConstTree Leaf(ValueType element) {
    return ConstTree(
        ConstructorAccessTag(), Block::Leaf(std::move(element)).Share(),
        std::unique_ptr<ConstTree>(), std::unique_ptr<ConstTree>());
  }

  static Ptr Append(const Ptr& a, const Ptr& b) {
    if (a == nullptr) return b;
    if (b == nullptr) return a;
    return Append(*a, *b).Share();
  }

  // Efficient construction, which runs in linear time.
  // TODO: Get rid of this? It is no longer very efficient.
  template <typename Iterator>
  static Ptr FromRange(Iterator begin, Iterator end) {
    if (begin == end) return nullptr;
    Iterator middle = begin + std::distance(begin, end) / 2;
    return FixBlocks(
               MakePtrVariant(Block::Leaf(std::forward<ValueType>(*middle))),
               FromRange(begin, middle), FromRange(middle + 1, end))
        .Share();
  }

  static Ptr PushBack(const Ptr& a, ValueType element) {
    return FixBlocks(MakePtrVariant(Block::Leaf(std::move(element))), a,
                     nullptr)
        .Share();
  }

  static Ptr Insert(const Ptr& tree, size_t index, ValueType element) {
    CHECK_LE(index, Size(tree));
    return (tree == nullptr ? Leaf(std::move(element))
                            : tree->Insert(index, std::move(element)))
        .Share();
  }

  static Ptr Erase(const Ptr& tree, size_t index) {
    CHECK_LE(index, Size(tree));
    if (Size(tree) == 1) return nullptr;
    return tree->Erase(index).Share();
  }

  Ptr Replace(size_t index, ValueType element) const {
    VLOG(6) << "Replace: " << index;
    auto size_left = Size(left_);
    if (index < size_left)
      return New(block_, left_->Replace(index, std::move(element)), right_);
    index -= size_left;

    if (index < block_->size())
      return New(block_->Replace(index, std::move(element)).Share(), left_,
                 right_);
    index -= block_->size();

    return New(block_, left_, right_->Replace(index, std::move(element)));
  }

  static inline size_t Size(const Ptr& tree) {
    return tree == nullptr ? 0 : tree->size_;
  }

  static inline size_t Depth(const Ptr& tree) {
    return tree == nullptr ? 0 : tree->depth_;
  }

  const ValueType& Get(size_t index) const {
    CHECK_LT(index, size_);
    auto size_left = Size(left_);
    if (index < size_left) {
      CHECK(left_ != nullptr);
      return left_->Get(index);
    }
    index -= size_left;

    if (index < block_->size()) {
      return block_->Get(index);
    }
    index -= block_->size();

    CHECK(right_ != nullptr);
    return right_->Get(index);
  }

  // Returns a tree containing the first len elements. Prefix("abcde", 2) ==
  // "ab".
  static Ptr Prefix(const Ptr& a, size_t len) {
    if (len == Size(a)) return a;
    if (len == 0) return nullptr;
    CHECK(a != nullptr);
    return a->Prefix(len).Share();
  }

  ConstTree Prefix(size_t len) const {
    CHECK_LE(len, size_);
    if (len == size_) return Copy();
    CHECK_GT(len, 0ul);
    auto size_left = Size(left_);
    if (len <= size_left) return left_->Prefix(len);
    len -= size_left;
    if (len < block_->size())
      return Rebalance(block_->Prefix(len).Share(), left_,
                       std::unique_ptr<ConstTree>());
    len -= block_->size();
    return Rebalance(block_, left_,
                     len == 0 ? nullptr : right_->Prefix(len).Share());
  }

  // Returns a tree skipping the first len elements (i.e., from element `len`
  // to the end).
  static Ptr Suffix(const Ptr& a, size_t len) {
    if (len >= Size(a)) return nullptr;
    CHECK(a != nullptr);
    return a->Suffix(len).Share();
  }

  ConstTree Suffix(size_t len) const {
    auto size_left = Size(left_);
    if (len < size_left)
      return FixBlocks(block_, left_->Suffix(len).Share(), right_);
    len -= size_left;
    if (len == 0) return FixBlocks(block_, nullptr, right_);
    if (len < block_->size())
      return FixBlocks(block_->Suffix(len).Share(), nullptr, right_);
    len -= block_->size();
    return right_->Suffix(len);
  }

  // Similar to std::upper_bound(begin(), end(), val, compare). Returns the
  // index of the first element greater than key. The elements in the tree
  // must be sorted (according to the less_than value given).
  template <typename LessThan>
  static size_t UpperBound(const Ptr& tree, const ValueType& key,
                           LessThan less_than) {
    return tree == nullptr ? 0 : tree->UpperBound(key, less_than);
  }

  template <typename LessThan>
  size_t UpperBound(const ValueType& key, LessThan& less_than) const {
    if (less_than(key, block_->Get(0))) {
      return UpperBound(left_, key, less_than);
    } else if (less_than(key, block_->Get(block_->size() - 1))) {
      return Size(left_) + block_->UpperBound(key, less_than);
    } else {
      return Size(left_) + block_->size() + UpperBound(right_, key, less_than);
    }
  }

  template <typename Predicate>
  static bool Every(const Ptr& tree, const Predicate& predicate) {
    if (tree == nullptr) return true;
    return Every(tree->left_, predicate) &&
           Block::Every(tree->block_, predicate) &&
           Every(tree->right_, predicate);
  }

  inline ConstTree Insert(size_t index, ValueType element) const {
    size_t size_left = Size(left_);
    if (index < size_left)
      return Rebalance(block_, left_->Insert(index, std::move(element)).Share(),
                       right_);

    index -= size_left;
    if (index > block_->size()) {
      index -= block_->size();
      CHECK(right_ != nullptr);
      return Rebalance(block_, left_,
                       right_->Insert(index, std::move(element)).Share());
    }

    CHECK_LE(index, block_->size());
    return MaybeSplitBlock(block_->Insert(index, std::move(element)), left_,
                           right_);
  }

  inline ConstTree Erase(size_t index) const {
    auto size_left = Size(left_);
    if (index < size_left)
      return FixBlocks(block_, Erase(left_, index), right_);
    index -= size_left;

    if (index >= block_->size()) {
      index -= block_->size();
      return Rebalance(block_, left_, Erase(right_, index));
    }

    if (block_->size() > 1) {
      return FixBlocks(MakePtrVariant(block_->Erase(index)), left_, right_);
    }

    CHECK(left_ != nullptr);
    return Rebalance(left_->block_, left_->left_,
                     Append(left_->right_, right_));
  }

  static ConstTree Append(const ConstTree& a, const ConstTree& b) {
    return FixBlocks(a.LastBlock(), a.MinusLastBlock(), ConstTree(b).Share());
  }

  size_t size() const { return size_; }

  static std::pair<ConstTree, ConstTree> Split(ConstTree input) {
    const size_t split_index = input.size() / 2;
    CHECK_GT(split_index, 0ul);
    return {input.Prefix(split_index), input.Suffix(split_index)};
  }

 private:
  const std::shared_ptr<const Block>& LastBlock() const {
    return right_ == nullptr ? block_ : right_->LastBlock();
  }

  const std::shared_ptr<const Block>& FirstBlock() const {
    return left_ == nullptr ? block_ : left_->FirstBlock();
  }

  Ptr MinusLastBlock() const {
    return right_ == nullptr ? left_
                             : std::make_shared<ConstTree>(Rebalance(
                                   block_, left_, right_->MinusLastBlock()));
  }

  Ptr MinusFirstBlock() const {
    return left_ == nullptr ? right_
                            : std::make_shared<ConstTree>(Rebalance(
                                  block_, left_->MinusFirstBlock(), right_));
  }

  Ptr RotateRight() const {
    CHECK(left_ != nullptr);
    return New(left_->block_, left_->left_, New(block_, left_->right_, right_));
  }

  Ptr RotateLeft() const {
    CHECK(right_ != nullptr);
    return New(right_->block_, New(block_, left_, right_->left_),
               right_->right_);
  }

  static ConstTree MaybeSplitBlock(Block block, Ptr left, Ptr right) {
    if (block.size() <= MaxBlockSize) {
      return FixBlocks(std::move(block).Share(), left, right);
    }
    CHECK_LT(block.size(), 2 * MaxBlockSize);
    std::pair<Block, Block> blocks = Block::Split(std::move(block));
    CHECK_LE(blocks.first.size(), MaxBlockSize);
    CHECK_LE(blocks.second.size(), MaxBlockSize);
    return Rebalance(
        std::move(blocks.second).Share(),
        Rebalance(std::move(blocks.first).Share(), left, nullptr).Share(),
        right);
  }

  static ConstTree FixBlocks(PtrVariant<Block> block, Ptr left, Ptr right) {
    if (left != nullptr) {
      if (const std::shared_ptr<const Block>& last_block_left =
              left->LastBlock();
          last_block_left->size() < MaxBlockSize / 2) {
        return MaybeSplitBlock(Block::Append(last_block_left->Copy(),
                                             ToUniqueValue(std::move(block))),
                               left->MinusLastBlock(), right);
      }
    }
    if (right != nullptr && AddressOf(block)->size() < MaxBlockSize / 2)
      return MaybeSplitBlock(Block::Append(ToUniqueValue(std::move(block)),
                                           right->FirstBlock()->Copy()),
                             left, right->MinusFirstBlock());

    VLOG(6) << "Creating without fixing blocks.";
    return Rebalance(ToSharedConst(std::move(block)), left, right);
  }

  static ConstTree Rebalance(std::shared_ptr<const Block> block, Ptr left,
                             Ptr right) {
    ValidateHalfFullInvariant(left.get(), true);
    ValidateHalfFullInvariant(right.get(), true);
    if (Depth(right) > Depth(left) + 1) {
      if (Depth(right->left_) > Depth(right->right_)) {
        right = right->RotateRight();
      }
      return ConstTree(ConstructorAccessTag(), right->block_,
                       MakePtrVariant(Rebalance(block, left, right->left_)),
                       right->right_);
    } else if (Depth(left) > Depth(right) + 1) {
      if (Depth(left->right_) > Depth(left->left_)) {
        left = left->RotateLeft();
      }
      return ConstTree(ConstructorAccessTag(), left->block_, left->left_,
                       MakePtrVariant(Rebalance(block, left->right_, right)));
    }
    return ConstTree(ConstructorAccessTag(), std::move(block), left, right);
  }

  static Ptr New(std::shared_ptr<const Block> block, Ptr left, Ptr right) {
    return std::make_shared<ConstTree>(ConstructorAccessTag{}, std::move(block),
                                       std::move(left), std::move(right));
  }

  static void ValidateHalfFullInvariant(const ConstTree* t, bool exclude_last) {
    if constexpr (ExpensiveValidation) {
      if (t == nullptr) return;
      ValidateHalfFullInvariant(t->left_.get(), false);
      if (t->right_ != nullptr || !exclude_last) {
        CHECK_GE(t->block_->size(), MaxBlockSize / 2);
      }
      ValidateHalfFullInvariant(t->right_.get(), exclude_last);
    }
  }

  // Every block in left_, right_ and block_ excluding the very last block
  // (either the last block in `right_` or, if `right_` is nullptr, `block_`)
  // must be at least half full.
  const std::shared_ptr<const Block> block_;
  const Ptr left_;
  const Ptr right_;

  const size_t depth_;
  const size_t size_;
};
}  // namespace afc::language

#endif  //  __AFC_LANGUAGE_CONST_TREE_H__
