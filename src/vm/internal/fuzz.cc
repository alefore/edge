#include <glog/logging.h>

#include "src/concurrent/thread_pool.h"
#include "src/futures/futures.h"
#include "src/language/gc.h"
#include "src/language/safe_types.h"
#include "src/vm/public/environment.h"
#include "src/vm/public/types.h"
#include "src/vm/public/value.h"
#include "src/vm/public/vm.h"

namespace gc = afc::language::gc;

using afc::infrastructure::Path;
using afc::language::FromByteString;
using afc::language::MakeNonNullUnique;
using afc::language::ValueOrDie;

int main(int, char** argv) {
  google::InitGoogleLogging(argv[0]);

  std::wstring error;
  afc::language::gc::Pool pool(afc::language::gc::Pool::Options{
      .collect_duration_threshold = std::nullopt,
      .thread_pool =
          std::make_shared<afc::concurrent::ThreadPool>(6, nullptr)});
  gc::Root<afc::vm::Environment> environment =
      pool.NewRoot(MakeNonNullUnique<afc::vm::Environment>());
  auto expr = afc::vm::CompileFile(
      ValueOrDie(Path::FromString(FromByteString("/dev/"
                                                 "stdin"))),
      pool, environment);
  if (IsError(expr)) {
    return 0;
  }

  std::function<void()> resume;
  auto value = afc::vm::Evaluate(std::get<0>(expr).value(), pool, environment,
                                 [&resume](std::function<void()> callback) {
                                   resume = std::move(callback);
                                 });
  expr = afc::language::Error(L"Done.");  // To release expr.

  for (int i = 0; i < 5 && resume != nullptr; ++i) {
    auto copy = std::move(resume);
    resume = nullptr;
    copy();
  }

  return 0;
}
