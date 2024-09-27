#include "src/language/const_tree.h"

#include "src/infrastructure/time.h"
#include "src/tests/benchmarks.h"
#include "src/tests/tests.h"

using afc::tests::BenchmarkName;

namespace afc::language {
namespace {
using infrastructure::Now;
// using IntTree = ConstTree<VectorBlock<int, 256>, 256>;
static constexpr int Base = 128;
using IntTree = ConstTree<ConstTree<VectorBlock<int, Base>, Base>, 256 * Base>;
using infrastructure::SecondsBetween;

const int kNumberToInsert = 25;

std::vector<size_t> RandomIndices(size_t output_size, size_t elements) {
  std::vector<size_t> indices;
  for (size_t i = 0; i < output_size; i++) {
    indices.push_back(random() % elements);
  }
  CHECK_EQ(indices.size(), output_size);
  return indices;
}

IntTree::Ptr GetTree(int size) {
  IntTree::Ptr tree;
  for (int i = 0; i < size; ++i) {
    tree =
        IntTree::Insert(tree, random() % (i + 1), kNumberToInsert).get_shared();
  }
  return tree;
}

bool registration_add = tests::RegisterBenchmark(
    BenchmarkName{L"ConstTree::PushBack"}, [](int elements) {
      auto tree = GetTree(elements);
      auto start = Now();
      tree = IntTree::PushBack(tree, 0).get_shared();
      auto end = Now();
      CHECK_EQ(IntTree::Size(tree), static_cast<size_t>(elements) + 1);
      return SecondsBetween(start, end);
    });

bool registration_prefix = tests::RegisterBenchmark(
    BenchmarkName{L"ConstTree::Prefix"}, [](int elements) {
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

bool registration_suffix = tests::RegisterBenchmark(
    BenchmarkName{L"ConstTree::Suffix"}, [](int elements) {
      auto tree = GetTree(elements);
      static const int kRuns = 1e5;
      auto start = Now();
      for (int i = 0; i < kRuns; i++) {
        size_t position = random() % elements;
        CHECK_EQ(IntTree::Size(IntTree::Suffix(tree, position)),
                 elements - position);
      }
      auto end = Now();
      return SecondsBetween(start, end) / kRuns;
    });

bool registration_tree_insert = tests::RegisterBenchmark(
    BenchmarkName{L"ConstTree::Insert"}, [](int elements) {
      auto tree = GetTree(elements);
      static const int kRuns = 1e5;
      auto indices = RandomIndices(kRuns, elements);
      auto start = Now();
      for (auto& index : indices) {
        CHECK_EQ(
            IntTree::Size(
                IntTree::Insert(tree, index, kNumberToInsert).get_shared()),
            static_cast<size_t>(elements) + 1);
      }
      auto end = Now();
      return SecondsBetween(start, end) / kRuns;
    });

bool registration_vector_insert = tests::RegisterBenchmark(
    BenchmarkName{L"Vector::Insert"}, [](int elements) {
      std::vector<int> v(elements);
      auto start = Now();
      int position = random() % (elements + 1);
      v.insert(v.begin() + position, 0);
      CHECK_EQ(v.size(), static_cast<size_t>(elements) + 1);
      auto end = Now();
      return SecondsBetween(start, end);
    });

bool registration_append = tests::RegisterBenchmark(
    BenchmarkName{L"ConstTree::Append"}, [](int elements) {
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

bool registration_vector_append = tests::RegisterBenchmark(
    BenchmarkName{L"Vector::Append"}, [](int elements) {
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

double RunGet(const IntTree::Ptr& tree, const std::vector<size_t>& indices) {
  auto start = Now();
  for (auto& index : indices) {
    CHECK_EQ(tree->Get(index), kNumberToInsert);
  }
  auto end = Now();
  return SecondsBetween(start, end) / indices.size();
}

bool registration_get = tests::RegisterBenchmark(
    BenchmarkName{L"ConstTree::Get"}, [](int elements) {
      static const size_t kRuns = 1e5;
      return RunGet(GetTree(elements), RandomIndices(kRuns, elements));
    });

bool registration_get_first = tests::RegisterBenchmark(
    BenchmarkName{L"ConstTree::GetFirst"}, [](int elements) {
      static const size_t kRuns = 1e5;
      std::vector<size_t> indices(kRuns);
      CHECK_EQ(indices.size(), kRuns);
      return RunGet(GetTree(elements), indices);
    });

bool registration_get_middle = tests::RegisterBenchmark(
    BenchmarkName{L"ConstTree::GetMiddle"}, [](int elements) {
      static const size_t kRuns = 1e5;
      std::vector<size_t> indices(kRuns, elements / 2);
      CHECK_EQ(indices.size(), kRuns);
      return RunGet(GetTree(elements), indices);
    });

bool registration_vector_get =
    tests::RegisterBenchmark(BenchmarkName{L"Vector::Get"}, [](int elements) {
      std::vector<int> v(elements);
      static const int kRuns = 1e5;
      auto start = Now();
      for (int i = 0; i < kRuns; i++) {
        v.at(random() % elements);
      }
      auto end = Now();
      return SecondsBetween(start, end) / kRuns;
    });

bool registration_erase = tests::RegisterBenchmark(
    BenchmarkName{L"ConstTree::Erase"}, [](int elements) {
      static const size_t kRuns = 1e5;
      auto indices = RandomIndices(kRuns, elements);
      auto tree = GetTree(elements);
      auto start = Now();
      for (auto& index : indices) {
        CHECK_EQ(IntTree::Size(IntTree::Erase(
                     NonNull<IntTree::Ptr>::Unsafe(tree), index)),
                 static_cast<size_t>(elements - 1));
      }
      auto end = Now();
      return SecondsBetween(start, end) / indices.size();
    });

bool registration_every = tests::RegisterBenchmark(
    BenchmarkName{L"ConstTree::Every"}, [](int elements) {
      auto tree = GetTree(elements);
      auto start = Now();
      CHECK(IntTree::Every(tree, [](int) { return true; }));
      auto end = Now();
      return SecondsBetween(start, end);
    });

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
