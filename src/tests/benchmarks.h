#ifndef __AFC_EDITOR_TESTS_BENCHMARKS_H__
#define __AFC_EDITOR_TESTS_BENCHMARKS_H__

#include <algorithm>
#include <fstream>
#include <functional>
#include <memory>

#include "src/language/ghost_type_class.h"
#include "src/language/lazy_string/single_line.h"

namespace afc::tests {
struct BenchmarkName
    : public language::GhostType<BenchmarkName,
                                 language::lazy_string::NonEmptySingleLine> {
  using GhostType::GhostType;
};

using BenchmarkSize = size_t;
using BenchmarkFunction = std::function<double(BenchmarkSize)>;

bool RegisterBenchmark(BenchmarkName name, BenchmarkFunction benchmark);

void RunBenchmark(BenchmarkName name);

std::vector<BenchmarkName> BenchmarkNames();

}  // namespace afc::tests

#endif  // __AFC_EDITOR_TESTS_BENCHMARKS_H__
