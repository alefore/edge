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

#include "src/concurrent/thread_pool.h"
#include "src/infrastructure/dirname.h"
#include "src/language/error/value_or_error.h"
#include "src/language/gc.h"
#include "src/language/lazy_string/char_buffer.h"
#include "src/language/lazy_string/lazy_string.h"
#include "src/language/wstring.h"
#include "src/vm/default_environment.h"
#include "src/vm/environment.h"
#include "src/vm/vm.h"

using afc::concurrent::OperationFactory;
using afc::concurrent::ThreadPool;
using afc::infrastructure::Path;
using afc::language::FromByteString;
using afc::language::MakeNonNullShared;
using afc::language::ValueOrDie;
using afc::language::lazy_string::LazyString;

int main(int, char** argv) {
  google::InitGoogleLogging(argv[0]);
  std::wifstream input(argv[1]);
  afc::language::gc::Pool pool(afc::language::gc::Pool::Options{
      .collect_duration_threshold = std::nullopt,
      .operation_factory = std::make_shared<OperationFactory>(
          MakeNonNullShared<afc::concurrent::ThreadPool>(6))});

  afc::vm::CompileFile(
      ValueOrDie(Path::New(LazyString{FromByteString(argv[1])})),
      afc::vm::NewDefaultEnvironment(pool).ptr());
  return 0;
}
