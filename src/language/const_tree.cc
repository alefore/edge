#include "src/language/const_tree.h"

#include "src/tests/tests.h"

namespace afc::language {
namespace {
// using IntTree = ConstTree<VectorBlock<int, 256>, 256>;
static constexpr int Base = 128;
using IntTree = ConstTree<ConstTree<VectorBlock<int, Base>, Base>, 256 * Base>;

bool IsEqual(const std::vector<int>& v, const IntTree::Ptr& tree) {
  if (v.size() != IntTree::Size(tree)) return false;
  for (size_t i = 0; i < v.size(); ++i) {
    if (v.at(i) != tree->Get(i)) return false;
  }
  return true;
}

IntTree::Ptr EraseWithAppend(IntTree::Ptr tree, size_t position) {
  return IntTree::Append(IntTree::Prefix(tree, position),
                         IntTree::Suffix(tree, position + 1));
}

const bool const_tree_tests_registration = tests::Register(
    L"ConstTreeTests",
    {// Tests that the invariants (about balance of the tree) hold and that the
     // results are the same as what happens when they're applied directly to a
     // vector.
     {.name = L"RandomWalk", .callback = [] {
        IntTree::Ptr tree;
        std::vector<int> v;
        while (IntTree::Size(tree) < 1e3) {
          size_t position = random() % (IntTree::Size(tree) + 1);
          int number = random();
          tree = IntTree::Insert(tree, position, number).get_shared();
          v.insert(v.begin() + position, number);
        }
        CHECK(IsEqual(v, tree));
        auto v_copy = v;
        auto tree_copy = tree;
        while (tree_copy != nullptr) {
          tree_copy =
              EraseWithAppend(tree_copy, random() % (IntTree::Size(tree_copy)));
          CHECK(IsEqual(v, tree));
        }

        tree_copy = tree;
        while (tree_copy != nullptr) {
          tree_copy = IntTree::Erase(NonNull<IntTree::Ptr>::Unsafe(tree_copy),
                                     random() % (IntTree::Size(tree_copy)));
          CHECK(IsEqual(v, tree));
        }
      }}});
}  // namespace
}  // namespace afc::language
