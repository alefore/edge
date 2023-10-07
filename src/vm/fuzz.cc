#include <glog/logging.h>

#include "src/concurrent/operation.h"
#include "src/concurrent/thread_pool.h"
#include "src/futures/futures.h"
#include "src/language/gc.h"
#include "src/language/safe_types.h"
#include "src/vm/environment.h"
#include "src/vm/types.h"
#include "src/vm/value.h"
#include "src/vm/vm.h"

namespace gc = afc::language::gc;

using afc::concurrent::OperationFactory;
using afc::concurrent::ThreadPool;
using afc::infrastructure::Path;
using afc::language::FromByteString;
using afc::language::MakeNonNullShared;
using afc::language::MakeNonNullUnique;
using afc::language::NonNull;
using afc::language::ValueOrDie;
using afc::language::ValueOrError;
using afc::vm::Expression;

int main(int, char** argv) {
  google::InitGoogleLogging(argv[0]);

  std::wstring error;
  afc::language::gc::Pool pool(afc::language::gc::Pool::Options{
      .collect_duration_threshold = std::nullopt,
      .operation_factory = std::make_shared<OperationFactory>(
          MakeNonNullShared<ThreadPool>(6))});
  gc::Root<afc::vm::Environment> environment =
      pool.NewRoot(MakeNonNullUnique<afc::vm::Environment>());
  ValueOrError<NonNull<std::shared_ptr<Expression>>> expr =
      ValueOrDie(afc::vm::CompileFile(
          ValueOrDie(Path::FromString(FromByteString("/dev/"
                                                     "stdin"))),
          pool, environment));
  if (IsError(expr)) {
    return 0;
  }

  std::function<void()> resume;
  auto value = afc::vm::Evaluate(ValueOrDie(std::move(expr)), pool, environment,
                                 [&resume](std::function<void()> callback) {
                                   resume = std::move(callback);
                                 });

  for (int i = 0; i < 5 && resume != nullptr; ++i) {
    auto copy = std::move(resume);
    resume = nullptr;
    copy();
  }

  return 0;
}
