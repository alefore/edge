#include <glog/logging.h>

#include <csignal>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>

extern "C" {
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
}

#include "src/buffer_contents.h"
#include "src/concurrent/thread_pool.h"
#include "src/cpp_parse_tree.h"
#include "src/infrastructure/dirname.h"
#include "src/language/gc.h"
#include "src/language/lazy_string/char_buffer.h"
#include "src/language/lazy_string/lazy_string.h"
#include "src/language/error/value_or_error.h"
#include "src/language/wstring.h"
#include "src/vm/public/environment.h"
#include "src/vm/public/vm.h"

using namespace afc::editor;

using afc::infrastructure::Path;
using afc::language::FromByteString;
using afc::language::ValueOrDie;

int main(int, char** argv) {
  google::InitGoogleLogging(argv[0]);
  std::wifstream input(argv[1]);
  afc::language::gc::Pool pool(afc::language::gc::Pool::Options{
      .collect_duration_threshold = std::nullopt,
      .thread_pool =
          std::make_shared<afc::concurrent::ThreadPool>(6, nullptr)});

  afc::vm::CompileFile(ValueOrDie(Path::FromString(FromByteString(argv[1]))),
                       pool, afc::vm::Environment::NewDefault(pool));
  return 0;
}
