#include "src/infrastructure/time.h"
#include "src/language/const_tree.h"
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
    BenchmarkName{NON_EMPTY_SINGLE_LINE_CONSTANT(L"ConstTree::PushBack")},
    [](int elements) {
      auto tree = GetTree(elements);
      auto start = Now();
      tree = IntTree::PushBack(tree, 0).get_shared();
      auto end = Now();
      CHECK_EQ(IntTree::Size(tree), static_cast<size_t>(elements) + 1);
      return SecondsBetween(start, end);
    });

bool registration_prefix = tests::RegisterBenchmark(
    BenchmarkName{NON_EMPTY_SINGLE_LINE_CONSTANT(L"ConstTree::Prefix")},
    [](int elements) {
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
    BenchmarkName{NON_EMPTY_SINGLE_LINE_CONSTANT(L"ConstTree::Suffix")},
    [](int elements) {
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
    BenchmarkName{NON_EMPTY_SINGLE_LINE_CONSTANT(L"ConstTree::Insert")},
    [](int elements) {
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
    BenchmarkName{NON_EMPTY_SINGLE_LINE_CONSTANT(L"Vector::Insert")},
    [](int elements) {
      std::vector<int> v(elements);
      auto start = Now();
      int position = random() % (elements + 1);
      v.insert(v.begin() + position, 0);
      CHECK_EQ(v.size(), static_cast<size_t>(elements) + 1);
      auto end = Now();
      return SecondsBetween(start, end);
    });

bool registration_append = tests::RegisterBenchmark(
    BenchmarkName{NON_EMPTY_SINGLE_LINE_CONSTANT(L"ConstTree::Append")},
    [](int elements) {
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
    BenchmarkName{NON_EMPTY_SINGLE_LINE_CONSTANT(L"Vector::Append")},
    [](int elements) {
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
    BenchmarkName{NON_EMPTY_SINGLE_LINE_CONSTANT(L"ConstTree::Get")},
    [](int elements) {
      static const size_t kRuns = 1e5;
      return RunGet(GetTree(elements), RandomIndices(kRuns, elements));
    });

bool registration_get_first = tests::RegisterBenchmark(
    BenchmarkName{NON_EMPTY_SINGLE_LINE_CONSTANT(L"ConstTree::GetFirst")},
    [](int elements) {
      static const size_t kRuns = 1e5;
      std::vector<size_t> indices(kRuns);
      CHECK_EQ(indices.size(), kRuns);
      return RunGet(GetTree(elements), indices);
    });

bool registration_get_middle = tests::RegisterBenchmark(
    BenchmarkName{NON_EMPTY_SINGLE_LINE_CONSTANT(L"ConstTree::GetMiddle")},
    [](int elements) {
      static const size_t kRuns = 1e5;
      std::vector<size_t> indices(kRuns, elements / 2);
      CHECK_EQ(indices.size(), kRuns);
      return RunGet(GetTree(elements), indices);
    });

bool registration_vector_get = tests::RegisterBenchmark(
    BenchmarkName{NON_EMPTY_SINGLE_LINE_CONSTANT(L"Vector::Get")},
    [](int elements) {
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
    BenchmarkName{NON_EMPTY_SINGLE_LINE_CONSTANT(L"ConstTree::Erase")},
    [](int elements) {
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
    BenchmarkName{NON_EMPTY_SINGLE_LINE_CONSTANT(L"ConstTree::Every")},
    [](int elements) {
      auto tree = GetTree(elements);
      auto start = Now();
      CHECK(IntTree::Every(tree, [](int) { return true; }));
      auto end = Now();
      return SecondsBetween(start, end);
    });

}  // namespace
}  // namespace afc::language
