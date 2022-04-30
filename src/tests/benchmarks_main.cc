#include <glog/logging.h>

#include "src/language/wstring.h"
#include "src/tests/benchmarks.h"

using afc::language::FromByteString;

int main(int argc, const char** argv) {
  using namespace afc::editor;
  using std::cerr;

  google::InitGoogleLogging(argv[0]);
  google::InstallFailureSignalHandler();

  srand(time(NULL));

  if (argc == 2) {
    afc::tests::RunBenchmark(FromByteString(argv[1]));
    exit(0);
  }

  std::cerr << "Usage: " << argv[0] << " BENCHMARK" << std::endl;
  std::cerr << "BENCHMARK must be one of: " << std::endl;
  for (auto& name : afc::tests::BenchmarkNames()) {
    std::cerr << name << std::endl;
  }
  exit(1);
}
