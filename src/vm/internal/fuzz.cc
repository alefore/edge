#include <glog/logging.h>

#include "src/futures/futures.h"
#include "src/language/gc.h"
#include "src/language/safe_types.h"
#include "src/vm/public/environment.h"
#include "src/vm/public/types.h"
#include "src/vm/public/value.h"
#include "src/vm/public/vm.h"

namespace gc = afc::language::gc;

using afc::language::MakeNonNullUnique;

int main(int, char** argv) {
  google::InitGoogleLogging(argv[0]);

  std::wstring error;
  gc::Pool pool;
  gc::Root<afc::vm::Environment> environment =
      pool.NewRoot(MakeNonNullUnique<afc::vm::Environment>());
  auto expr = afc::vm::CompileFile(
      "/dev/"
      "stdin",
      pool, environment);
  if (IsError(expr)) {
    return 0;
  }

  std::function<void()> resume;
  auto value = afc::vm::Evaluate(expr.value().value(), pool, environment,
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
