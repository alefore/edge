#include "src/const_tree.h"

#include "src/tests/benchmarks.h"
#include "src/tests/tests.h"
#include "src/time.h"

namespace afc::editor {
namespace {
using IntTree = ConstTree<int>;

IntTree::Ptr Insert(IntTree::Ptr tree, size_t position) {
  static const int kNumberToInsert = 25;
  return IntTree::Append(
      IntTree::PushBack(IntTree::Prefix(tree, position), kNumberToInsert),
      IntTree::Suffix(tree, position));
}

IntTree::Ptr GetTree(int size) {
  IntTree::Ptr tree;
  for (int i = 0; i < size; ++i) {
    tree = Insert(tree, random() % (i + 1));
  }
  return tree;
}

bool registration_add =
    tests::RegisterBenchmark(L"ConstTree::PushBack", [](int elements) {
      auto tree = GetTree(elements);
      auto start = Now();
      tree = IntTree::PushBack(tree, 0);
      auto end = Now();
      CHECK_EQ(IntTree::Size(tree), static_cast<size_t>(elements) + 1);
      return SecondsBetween(start, end);
    });

bool registration_prefix =
    tests::RegisterBenchmark(L"ConstTree::Prefix", [](int elements) {
      auto tree = GetTree(elements);
      auto start = Now();
      tree = IntTree::Prefix(tree, elements - 1);
      auto end = Now();
      CHECK_EQ(IntTree::Size(tree), static_cast<size_t>(elements) - 1);
      return SecondsBetween(start, end);
    });

bool registration_insert =
    tests::RegisterBenchmark(L"ConstTree::Insert", [](int elements) {
      auto tree = GetTree(elements);
      int position = random() % (elements + 1);
      auto start = Now();
      tree = Insert(tree, position);
      auto end = Now();
      CHECK_EQ(IntTree::Size(tree), static_cast<size_t>(elements) + 1);
      return SecondsBetween(start, end);
    });

IntTree::Ptr Remove(IntTree::Ptr tree, size_t position) {
  return IntTree::Append(IntTree::Prefix(tree, position),
                         IntTree::Suffix(tree, position + 1));
}

class ConstTreeTests : public tests::TestGroup<ConstTreeTests> {
 public:
  ConstTreeTests() : TestGroup<ConstTreeTests>() {}
  std::wstring Name() const override { return L"ConstTreeTests"; }
  std::vector<tests::Test> Tests() const override {
    // Tests that the invariants (about balance of the tree) hold.
    return {{.name = L"RandomWalk", .callback = [] {
               IntTree::Ptr tree;
               while (IntTree::Size(tree) < 1e4) {
                 tree = Insert(tree, random() % (IntTree::Size(tree) + 1));
               }
               while (tree != nullptr) {
                 tree = Remove(tree, random() % (IntTree::Size(tree)));
               }
             }}};
  }
};

template <>
const bool tests::TestGroup<ConstTreeTests>::registration_ =
    tests::Add<editor::ConstTreeTests>();
}  // namespace
}  // namespace afc::editor
