#include "src/tests/benchmarks.h"

#include "src/infrastructure/time.h"

namespace afc::tests {
namespace {
std::unordered_map<std::wstring, BenchmarkFunction>* benchmarks_map() {
  static std::unordered_map<std::wstring, BenchmarkFunction> output;
  return &output;
}
}  // namespace

bool RegisterBenchmark(std::wstring name, BenchmarkFunction benchmark) {
  CHECK(benchmark != nullptr);
  auto [_, result] = benchmarks_map()->insert({name, std::move(benchmark)});
  CHECK(result) << "Unable to insert benchmarks (repeated name?): " << name;
  return true;
}

void RunBenchmark(std::wstring name) {
  auto benchmark = benchmarks_map()->find(name);
  if (benchmark == benchmarks_map()->end()) {
    std::cerr << "Unknown Benchmark: " << name << std::endl;
    exit(1);
  }

  int input_size = 1;
  static const int kRuns = 5;
  while (true) {
    double total_seconds = 0;
    for (int i = 0; i < kRuns; i++) {
      total_seconds += benchmark->second(input_size);
    }
    std::cerr << input_size << " " << total_seconds / kRuns << std::endl;
    if (input_size * 2 < input_size) return;
    input_size *= 2;
  }
}

std::vector<std::wstring> BenchmarkNames() {
  std::vector<std::wstring> output;
  for (const auto& [name, _] : *benchmarks_map()) {
    output.push_back(name);
  }
  return output;
}
}  // namespace afc::tests
