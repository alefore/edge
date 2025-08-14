#include <glog/logging.h>

#include "src/concurrent/operation.h"
#include "src/concurrent/thread_pool.h"
#include "src/futures/futures.h"
#include "src/language/gc.h"
#include "src/language/lazy_string/lazy_string.h"
#include "src/language/once_only_function.h"
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
using afc::language::OnceOnlyFunction;
using afc::language::ValueOrDie;
using afc::language::ValueOrError;
using afc::language::lazy_string::LazyString;
using afc::vm::Expression;

int main(int, char** argv) {
  google::InitGoogleLogging(argv[0]);

  std::wstring error;
  afc::language::gc::Pool pool(afc::language::gc::Pool::Options{
      .collect_duration_threshold = std::nullopt,
      .operation_factory = std::make_shared<OperationFactory>(
          MakeNonNullShared<ThreadPool>(6))});
  gc::Root<afc::vm::Environment> environment = afc::vm::Environment::New(pool);
  ValueOrError<gc::Root<Expression>> expr = afc::vm::CompileFile(
      ValueOrDie(Path::New(LazyString{FromByteString("/dev/"
                                                     "stdin")})),
      environment.ptr());
  if (IsError(expr)) return 0;

  std::optional<OnceOnlyFunction<void()>> resume;
  auto value =
      afc::vm::Evaluate(VALUE_OR_DIE(std::move(expr)).ptr(), environment.ptr(),
                        [&resume](OnceOnlyFunction<void()> callback) {
                          resume = std::move(callback);
                        });

  for (int i = 0; i < 5 && resume.has_value(); ++i) {
    auto copy = std::move(resume.value());
    resume = std::nullopt;
    std::move(copy)();
  }

  return 0;
}
