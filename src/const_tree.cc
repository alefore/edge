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
      static const int kRuns = 1e5;
      auto start = Now();
      for (int i = 0; i < kRuns; i++) {
        size_t position = random() % elements;
        CHECK_EQ(IntTree::Size(IntTree::Prefix(tree, position)), position);
      }
      auto end = Now();
      return SecondsBetween(start, end) / kRuns;
    });

bool registration_tree_insert =
    tests::RegisterBenchmark(L"ConstTree::Insert", [](int elements) {
      auto tree = GetTree(elements);
      auto start = Now();
      int position = random() % (elements + 1);
      tree = Insert(tree, position);
      CHECK_EQ(IntTree::Size(tree), static_cast<size_t>(elements) + 1);
      auto end = Now();
      return SecondsBetween(start, end);
    });

bool registration_vector_insert =
    tests::RegisterBenchmark(L"Vector::Insert", [](int elements) {
      std::vector<int> v(elements);
      auto start = Now();
      int position = random() % (elements + 1);
      v.insert(v.begin() + position, 0);
      CHECK_EQ(v.size(), static_cast<size_t>(elements) + 1);
      auto end = Now();
      return SecondsBetween(start, end);
    });

bool registration_append =
    tests::RegisterBenchmark(L"ConstTree::Append", [](int elements) {
      if (elements < 8) return 0.0;
      auto left = GetTree(random() % elements);
      auto right = GetTree(elements - IntTree::Size(left));
      static const int kRuns = 1e5;
      auto start = Now();
      for (int i = 0; i < kRuns; i++) {
        auto tree = IntTree::Append(left, right);
        CHECK_EQ(IntTree::Size(tree), static_cast<size_t>(elements));
      }
      auto end = Now();
      return SecondsBetween(start, end) / kRuns;
    });

bool registration_vector_append =
    tests::RegisterBenchmark(L"Vector::Append", [](int elements) {
      if (elements < 8) return 0.0;
      std::vector<int> left(random() % elements);
      std::vector<int> right(elements - left.size());
      static const int kRuns = 1e5;
      auto start = Now();
      for (int i = 0; i < kRuns; i++) {
        auto output = left;
        output.insert(output.end(), right.begin(), right.end());
        CHECK_EQ(output.size(), static_cast<size_t>(elements));
      }
      auto end = Now();
      return SecondsBetween(start, end) / kRuns;
    });

bool registration_get =
    tests::RegisterBenchmark(L"ConstTree::Get", [](int elements) {
      auto tree = GetTree(elements);
      static const int kRuns = 1e5;
      auto start = Now();
      for (int i = 0; i < kRuns; i++) {
        tree->Get(random() % elements);
      }
      auto end = Now();
      return SecondsBetween(start, end) / kRuns;
    });

bool registration_vector_get =
    tests::RegisterBenchmark(L"Vector::Get", [](int elements) {
      std::vector<int> v(elements);
      static const int kRuns = 1e5;
      auto start = Now();
      for (int i = 0; i < kRuns; i++) {
        v.at(random() % elements);
      }
      auto end = Now();
      return SecondsBetween(start, end) / kRuns;
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
