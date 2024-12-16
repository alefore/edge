#include <glog/logging.h>

#include "src/language/lazy_string/single_line.h"
#include "src/language/wstring.h"
#include "src/tests/benchmarks.h"

using afc::language::FromByteString;
using afc::language::lazy_string::LazyString;
using afc::language::lazy_string::NonEmptySingleLine;
using afc::language::lazy_string::SingleLine;

int main(int argc, const char** argv) {
  using std::cerr;

  google::InitGoogleLogging(argv[0]);
  google::InstallFailureSignalHandler();

  srand(time(NULL));

  if (argc == 2) {
    afc::tests::RunBenchmark(afc::tests::BenchmarkName(
        NonEmptySingleLine(SingleLine(LazyString{FromByteString(argv[1])}))));
    exit(0);
  }

  std::cerr << "Usage: " << argv[0] << " BENCHMARK" << std::endl;
  std::cerr << "BENCHMARK must be one of: " << std::endl;
  for (auto& name : afc::tests::BenchmarkNames()) {
    std::cerr << name << std::endl;
  }
  exit(1);
}
