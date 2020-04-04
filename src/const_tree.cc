#include "src/const_tree.h"

#include "src/tests/benchmarks.h"
#include "src/time.h"

namespace afc::editor {

ConstTree<int>::Ptr GetTree(int size) {
  ConstTree<int>::Ptr tree;
  for (int i = 0; i < size; ++i) {
    tree = ConstTree<int>::PushBack(tree, 0);
  }
  return tree;
}

bool registration_add =
    tests::RegisterBenchmark(L"ConstTree::PushBack", [](int elements) {
      auto tree = GetTree(elements);
      auto start = Now();
      tree = ConstTree<int>::PushBack(tree, 0);
      auto end = Now();
      CHECK_EQ(ConstTree<int>::Size(tree), static_cast<size_t>(elements) + 1);
      return SecondsBetween(start, end);
    });

bool registration_prefix =
    tests::RegisterBenchmark(L"ConstTree::Prefix", [](int elements) {
      auto tree = GetTree(elements);
      auto start = Now();
      tree = ConstTree<int>::Prefix(tree, elements - 1);
      auto end = Now();
      CHECK_EQ(ConstTree<int>::Size(tree), static_cast<size_t>(elements) - 1);
      return SecondsBetween(start, end);
    });
}  // namespace afc::editor
