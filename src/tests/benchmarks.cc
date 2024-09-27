#include "src/tests/benchmarks.h"

#include <glog/logging.h>

#include "src/infrastructure/time.h"
#include "src/language/container.h"

namespace container = afc::language::container;

using afc::language::InsertOrDie;

namespace afc::tests {
namespace {
std::unordered_map<BenchmarkName, BenchmarkFunction>& benchmarks_map() {
  static std::unordered_map<BenchmarkName, BenchmarkFunction>* const output =
      new std::unordered_map<BenchmarkName, BenchmarkFunction>();
  return *output;
}
}  // namespace

bool RegisterBenchmark(BenchmarkName name, BenchmarkFunction benchmark) {
  CHECK(benchmark != nullptr);
  InsertOrDie(benchmarks_map(), {name, std::move(benchmark)});
  return true;
}

void RunBenchmark(BenchmarkName name) {
  auto benchmark = benchmarks_map().find(name);
  if (benchmark == benchmarks_map().end()) {
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

std::vector<BenchmarkName> BenchmarkNames() {
  return container::MaterializeVector(benchmarks_map() | std::views::keys);
}
}  // namespace afc::tests
