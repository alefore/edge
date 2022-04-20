#ifndef __AFC_EDITOR_TESTS_BENCHMARKS_H__
#define __AFC_EDITOR_TESTS_BENCHMARKS_H__

#include <algorithm>
#include <fstream>
#include <functional>
#include <memory>

#include "src/fuzz_testable.h"
#include "src/language/wstring.h"

namespace afc::tests {
using BenchmarkSize = size_t;
using BenchmarkFunction = std::function<double(BenchmarkSize)>;

bool RegisterBenchmark(std::wstring name, BenchmarkFunction benchmark);

void RunBenchmark(std::wstring name);

std::vector<std::wstring> BenchmarkNames();

}  // namespace afc::tests

#endif  // __AFC_EDITOR_TESTS_BENCHMARKS_H__
